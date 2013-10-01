// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/input.h"

#include <dirent.h>
#include <fcntl.h>
#include <libudev.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/input_observer.h"

using std::map;
using std::string;
using std::vector;

namespace power_manager {
namespace system {

namespace {

const char kInputUdevSubsystem[] = "input";
const char kSysClassInputPath[] = "/sys/class/input";
const char kDevInputPath[] = "/dev/input";
const char kEventBaseName[] = "event";
const char kInputBaseName[] = "input";

const char kWakeupDisabled[] = "disabled";
const char kWakeupEnabled[] = "enabled";

const char kInputMatchPattern[] = "input*";
const char kUsbMatchString[] = "usb";
const char kBluetoothMatchString[] = "bluetooth";

const char kConsolePath[] = "/dev/tty0";

// Path to an optional script used to disable touch devices when the lid is
// closed.
const char kTouchControlPath[] = "/opt/google/touch/touch-control.sh";

// Physical location (as returned by EVIOCGPHYS()) of power button devices that
// should be skipped.
#ifdef LEGACY_POWER_BUTTON
// Skip input events that are on the built-in keyboard. Many of these devices
// advertise a power button but do not physically have one. Skipping will reduce
// the wasteful waking of powerd due to keyboard events.
const char kPowerButtonToSkip[] = "isa";
#else
// Skip input events from the ACPI power button (identified as LNXPWRBN) if a
// new power button is present on the keyboard.
const char kPowerButtonToSkip[] = "LNXPWRBN";
#endif

// Sources of input events.
enum InputType {
  INPUT_LID,
  INPUT_POWER_BUTTON,
  INPUT_UNHANDLED,
};

InputType GetInputType(const struct input_event& event) {
  if (event.type == EV_KEY) {
    // For key events, only handle the keys listed below.
    switch (event.code) {
      case KEY_POWER:
        return INPUT_POWER_BUTTON;
      default:
        return INPUT_UNHANDLED;
    }
  }
  // For switch events, only handle events from the lid.
  if (event.type == EV_SW && event.code == SW_LID)
    return INPUT_LID;

  return INPUT_UNHANDLED;
}

bool GetSuffixNumber(const char* name, const char* base_name, int* suffix) {
  if (strncmp(base_name, name, strlen(base_name)))
    return false;
  return base::StringToInt(name + strlen(base_name), suffix);
}

}  // namespace

Input::Input()
    : lid_fd_(-1),
      num_power_key_events_(0),
      num_lid_events_(0),
      wakeups_enabled_(true),
      use_lid_(true),
      console_fd_(-1) {}

Input::~Input() {
  for (InputMap::iterator iter = registered_inputs_.begin();
       iter != registered_inputs_.end();
       ++iter) {
    CloseIOChannel(&iter->second);
  }

  if (lid_fd_ >= 0)
    close(lid_fd_);
  if (console_fd_ >= 0)
    close(console_fd_);
}

bool Input::Init(const vector<string>& wakeup_input_names, bool use_lid) {
  use_lid_ = use_lid;
  for (vector<string>::const_iterator names_iter = wakeup_input_names.begin();
      names_iter != wakeup_input_names.end(); ++names_iter) {
    // Iterate through the vector of input names, and if not the empty string,
    // put the input name into the wakeup_inputs_map_, mapping to -1.
    // This indicates looking for input devices with this name, but there
    // is no input number associated with the device just yet.
    if ((*names_iter).length() > 0)
      wakeup_inputs_map_[*names_iter] = -1;
  }

  // Don't bother doing anything if we're running under a test.
  if (!sysfs_input_path_for_testing_.empty())
    return true;

  if ((console_fd_ = open(kConsolePath, O_WRONLY)) == -1)
    PLOG(ERROR) << "Unable to open " << kConsolePath;

  RegisterUdevEventHandler();
  RegisterInputWakeSources();
  return RegisterInputDevices();
}

void Input::AddObserver(InputObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void Input::RemoveObserver(InputObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

#define BITS_PER_LONG (sizeof(long) * 8)
#define NUM_BITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x)  ((x) % BITS_PER_LONG)
#define BIT(x)  (1UL << OFF(x))
#define LONG(x) ((x) / BITS_PER_LONG)
#define IS_BIT_SET(bit, array)  ((array[LONG(bit)] >> OFF(bit)) & 1)

LidState Input::QueryLidState() {
  if (lid_fd_ < 0)
    return LID_NOT_PRESENT;

  unsigned long switch_events[NUM_BITS(SW_LID + 1)];
  memset(switch_events, 0, sizeof(switch_events));
  if (ioctl(lid_fd_, EVIOCGBIT(EV_SW, SW_LID + 1), switch_events) < 0) {
    PLOG(ERROR) << "Lid state ioctl() failed";
    return LID_NOT_PRESENT;
  }
  if (IS_BIT_SET(SW_LID, switch_events)) {
    ioctl(lid_fd_, EVIOCGSW(sizeof(switch_events)), switch_events);
    return IS_BIT_SET(SW_LID, switch_events) ? LID_CLOSED : LID_OPEN;
  } else {
    return LID_NOT_PRESENT;
  }
}

bool Input::IsUSBInputDeviceConnected() const {
  file_util::FileEnumerator enumerator(
      base::FilePath(sysfs_input_path_for_testing_.empty() ?
                     kSysClassInputPath : sysfs_input_path_for_testing_),
      false,
      static_cast<file_util::FileEnumerator::FileType>(
          file_util::FileEnumerator::FILES |
          file_util::FileEnumerator::SHOW_SYM_LINKS),
      kInputMatchPattern);
  for (base::FilePath path = enumerator.Next();
       !path.empty();
       path = enumerator.Next()) {
    base::FilePath symlink_path;
    if (!file_util::ReadSymbolicLink(path, &symlink_path))
      continue;
    const std::string& path_string = symlink_path.value();
    // Skip bluetooth devices, which may be identified as USB devices.
    if (path_string.find(kBluetoothMatchString) != std::string::npos)
      continue;
    // Now look for the USB devices that are not bluetooth.
    size_t position = path_string.find(kUsbMatchString);
    if (position == std::string::npos)
      continue;
    // Now that the string "usb" has been found, make sure it is a whole word
    // and not just part of another word like "busbreaker".
    bool usb_at_word_head =
        position == 0 || !IsAsciiAlpha(path_string.at(position - 1));
    bool usb_at_word_tail =
        position + strlen(kUsbMatchString) == path_string.size() ||
        !IsAsciiAlpha(path_string.at(position + strlen(kUsbMatchString)));
    if (usb_at_word_head && usb_at_word_tail)
      return true;
  }
  return false;
}

int Input::GetActiveVT() {
  struct vt_stat state;
  if (ioctl(console_fd_, VT_GETSTATE, &state) == -1) {
    PLOG(ERROR) << "VT_GETSTATE ioctl on " << kConsolePath << "failed";
    return -1;
  }
  return state.v_active;
}

bool Input::SetWakeInputsState(bool enable) {
  wakeups_enabled_ = enable;
  return SetInputWakeupStates();
}

void Input::SetTouchDevicesState(bool enable) {
  if (access(kTouchControlPath, R_OK | X_OK) != 0)
    return;

  if (enable)
    util::Launch(StringPrintf("%s --enable", kTouchControlPath));
  else
    util::Run(StringPrintf("%s --disable", kTouchControlPath));
}

bool Input::RegisterInputDevices() {
  base::FilePath input_path(kDevInputPath);
  DIR* dir = opendir(input_path.value().c_str());
  int num_registered = 0;
  if (dir) {
    struct dirent entry;
    struct dirent* result;
    while (readdir_r(dir, &entry, & result) == 0 && result) {
      if (result->d_name[0]) {
        if (AddEvent(result->d_name))
          num_registered++;
      }
    }
  } else {
    PLOG(ERROR) << "Cannot open input dir " << input_path.value().c_str();
    return false;
  }

  LOG(INFO) << "Number of power button events registered: "
            << num_power_key_events_;

  if (num_lid_events_ > 1) {
    LOG(ERROR) << "Saw multiple lid events; ignoring them all";
    return false;
  }
  LOG(INFO) << "Number of lid events registered: " << num_lid_events_;
  return true;
}

bool Input::RegisterInputWakeSources() {
  DIR* dir = opendir(kSysClassInputPath);
  if (dir) {
    struct dirent entry;
    struct dirent* result;
    while (readdir_r(dir, &entry, &result) == 0 && result) {
      if (result->d_name[0] &&
          strncmp(result->d_name, kInputBaseName, strlen(kInputBaseName)) == 0)
        AddWakeInput(result->d_name);
    }
  }
  return true;
}

bool Input::SetInputWakeupStates() {
  bool result = true;
  for (WakeupMap::iterator iter = wakeup_inputs_map_.begin();
       iter != wakeup_inputs_map_.end(); iter++) {
    int input_num = (*iter).second;
    if (input_num != -1 && !SetWakeupState(input_num, wakeups_enabled_)) {
      result = false;
      LOG(WARNING) << "Failed to set power/wakeup for input" << input_num;
    }
  }
  return result;
}

bool Input::SetWakeupState(int input_num, bool enabled) {
  // Allocate a buffer of size enough to fit the basename "input"
  // with an integer up to maxint.
  // The number of digits required to hold a number k represented in base b
  // is ceil(logb(k)).
  // In this case, base b=10 and k = 256^n, where n=number of bytes.
  // So number of digits = ceil(log10(256^n)) = ceil (n log10(256))
  // = ceil(2.408 n) <= 3 * n.
  // + 1 for null terminator.
  char name[strlen(kInputBaseName) + sizeof(input_num) * 3 + 1];

  snprintf(name, sizeof(name), "%s%d", kInputBaseName, input_num);
  base::FilePath input_path = base::FilePath(kSysClassInputPath).Append(name);

  // wakeup sysfs is at /sys/class/input/inputX/device/power/wakeup
  base::FilePath wakeup_path = input_path.Append("device/power/wakeup");
  if (access(wakeup_path.value().c_str(), R_OK)) {
    LOG(WARNING) << "Failed to access power/wakeup for " << name;
    return false;
  }

  const char* wakeup_str = enabled ? kWakeupEnabled : kWakeupDisabled;
  if (!file_util::WriteFile(wakeup_path, wakeup_str, strlen(wakeup_str))) {
    LOG(ERROR) << "Failed to write to power/wakeup";
    return false;
  }

  LOG(INFO) << "Set power/wakeup state for input" << input_num << ": "
            << wakeup_str;
  return true;
}

void Input::CloseIOChannel(IOChannelWatch* channel) {
  // The source id should not be invalid (see AddEvent()), so log a warning and
  // continue with the removal instead of failing or skipping.  We still want to
  // remove the IO channel itself even if the source is invalid.
  if (channel->source_id == 0)
    LOG(WARNING) << "Attempting to remove invalid glib source";
  else
    g_source_remove(channel->source_id);
  channel->source_id = 0;

  if (g_io_channel_shutdown(channel->channel, true, NULL) != G_IO_STATUS_NORMAL)
    LOG(WARNING) << "Error shutting down GIO channel";
  if (close(g_io_channel_unix_get_fd(channel->channel)) < 0)
    PLOG(ERROR) << "Error closing file handle";
  channel->channel = NULL;
}

bool Input::AddEvent(const char* name) {
  // Avoid logging warnings for files that should be ignored.
  const char* kEventsToSkip[] = {
    ".",
    "..",
    "by-path",
  };
  for (size_t i = 0; i < arraysize(kEventsToSkip); ++i) {
    if (strcmp(name, kEventsToSkip[i]) == 0)
      return false;
  }

  int event_num = -1;
  if (!GetSuffixNumber(name, kEventBaseName, &event_num)) {
    LOG(WARNING) << name << " is not a valid event name; not adding as event";
    return false;
  }

  InputMap::iterator iter = registered_inputs_.find(event_num);
  if (iter != registered_inputs_.end()) {
    LOG(WARNING) << "Input event " << event_num << " already registered";
    return false;
  }

  base::FilePath event_path = base::FilePath(kDevInputPath).Append(name);
  int event_fd;
  if (access(event_path.value().c_str(), R_OK)) {
    LOG(WARNING) << "Missing read access to " << event_path.value().c_str();
    return false;
  }
  if ((event_fd = open(event_path.value().c_str(), O_RDONLY)) < 0) {
    PLOG(ERROR) << "open() failed for " << event_path.value().c_str();
    return false;
  }

  if (!RegisterInputEvent(event_fd, event_num)) {
    if (close(event_fd) < 0)  // event not registered, closing.
      PLOG(ERROR) << "close() failed for " << event_path.value().c_str();
    return false;
  }
  return true;
}

bool Input::RemoveEvent(const char* name) {
  int event_num = -1;
  if (!GetSuffixNumber(name, kEventBaseName, &event_num)) {
    LOG(WARNING) << name << " is not a valid event name; not removing event";
    return false;
  }

  InputMap::iterator iter = registered_inputs_.find(event_num);
  if (iter == registered_inputs_.end()) {
    LOG(WARNING) << "Input event " << name << " not registered; "
                 << "nothing to remove";
    return false;
  }

  CloseIOChannel(&iter->second);
  registered_inputs_.erase(iter);
  return true;
}

bool Input::AddWakeInput(const char* name) {
  int input_num = -1;
  if (wakeup_inputs_map_.empty() ||
      !GetSuffixNumber(name, kInputBaseName, &input_num))
    return false;

  base::FilePath device_name_path =
      base::FilePath(kSysClassInputPath).Append(name).Append("name");
  if (access(device_name_path.value().c_str(), R_OK)) {
    LOG(WARNING) << "Failed to access input name";
    return false;
  }

  std::string input_name;
  if (!file_util::ReadFileToString(device_name_path, &input_name)) {
    LOG(WARNING) << "Failed to read input name";
    return false;
  }
  TrimWhitespaceASCII(input_name, TRIM_TRAILING, &input_name);
  WakeupMap::iterator iter = wakeup_inputs_map_.find(input_name);
  if (iter == wakeup_inputs_map_.end()) {
    // Not on the list of wakeup input devices
    return false;
  }

  if (!SetWakeupState(input_num, wakeups_enabled_)) {
    LOG(ERROR) << "Error adding wakeup source; cannot write to power/wakeup";
    return false;
  }
  wakeup_inputs_map_[input_name] = input_num;
  LOG(INFO) << "Added wakeup source " << name << " (" << input_name << ")";
  return true;
}

bool Input::RemoveWakeInput(const char* name) {
  int input_num = -1;
  if (wakeup_inputs_map_.empty() ||
      !GetSuffixNumber(name, kInputBaseName, &input_num))
    return false;

  WakeupMap::iterator iter;
  for (iter = wakeup_inputs_map_.begin();
       iter != wakeup_inputs_map_.end(); iter++) {
    if ((*iter).second == input_num) {
      wakeup_inputs_map_[(*iter).first] = -1;
      LOG(INFO) << "Removed wakeup source " << name
                << " (" << (*iter).first << ")";
    }
  }
  return false;
}

bool Input::RegisterInputEvent(int fd, int event_num) {
  char name[256] = "Unknown";
  if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
    PLOG(ERROR) << "Could not get name of device (FD " << fd << ", event "
                << event_num << ")";
    return false;
  } else {
    VLOG(1) << "Device name: " << name;
  }

  char phys[256] = "Unknown";
  if (ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) < 0) {
    PLOG(ERROR) << "Could not get topo phys path of device " << name;
    return false;
  } else {
    VLOG(1) << "Device topo phys: " << phys;
  }

  if (strncmp(kPowerButtonToSkip, phys, strlen(kPowerButtonToSkip)) == 0) {
    VLOG(1) << "Skipping interface: " << phys;
    return false;
  }

  unsigned long events[NUM_BITS(EV_MAX)];
  memset(events, 0, sizeof(events));
  if (ioctl(fd, EVIOCGBIT(0, EV_MAX), events) < 0) {
    PLOG(ERROR) << "EV_MAX ioctl failed for device " << name;
    return false;
  }

  GIOChannel* channel = NULL;
  guint watch_id = 0;
  bool watch_added = false;
  if (IS_BIT_SET(EV_KEY, events)) {
    unsigned long keys[NUM_BITS(KEY_MAX)];
    memset(keys, 0, sizeof(keys));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), keys) < 0)
      PLOG(ERROR) << "KEY_MAX ioctl failed for device " << name;
    if (IS_BIT_SET(KEY_POWER, keys)) {
      LOG(INFO) << "Watching " << phys << " (" << name << ") for power button";
      channel = g_io_channel_unix_new(fd);
      watch_id = g_io_add_watch(
          channel, G_IO_IN, &Input::HandleInputEventThunk, this);
      num_power_key_events_++;
      watch_added = true;
    }
  }
  if (IS_BIT_SET(EV_SW, events)) {
    unsigned long switch_events[NUM_BITS(SW_LID + 1)];
    memset(switch_events, 0, sizeof(switch_events));
    if (ioctl(fd, EVIOCGBIT(EV_SW, SW_LID + 1), switch_events) < 0)
      PLOG(ERROR) << "SW_LID ioctl failed for device " << name;
    // An input event may have more than one kind of key or switch.
    // For example, if both the power button and the lid switch are handled
    // by the gpio_keys driver, both will share a single event in /dev/input.
    // In this case, only create one io channel per fd, and only add one
    // watch per event file.
    if (use_lid_ && IS_BIT_SET(SW_LID, switch_events)) {
      num_lid_events_++;
      if (!watch_added) {
        LOG(INFO) << "Watching " << phys << " (" << name << ") for lid switch";
        channel = g_io_channel_unix_new(fd);
        watch_id = g_io_add_watch(
            channel, G_IO_IN, &(Input::HandleInputEventThunk), this);
        watch_added = true;
      } else {
        LOG(INFO) << "Watched event also has a lid";
      }
      if (lid_fd_ >= 0)
        LOG(WARNING) << "Multiple lid events found on system";
      lid_fd_ = fd;
    }
  }
  if (!watch_added)
    return false;

  IOChannelWatch desc(channel, watch_id);
  // The watch id should be valid if there was a successful event registration.
  // Thus, if the id turns out to be invalid, log a warning instead of failing
  // or skipping this part.
  LOG_IF(WARNING, watch_id == 0) << "Invalid glib source for event " << name;
  registered_inputs_[event_num] = desc;
  return true;
}

gboolean Input::HandleUdevEvent(GIOChannel* /* source */,
                                GIOCondition /* condition */) {
  struct udev_device* dev = udev_monitor_receive_device(udev_monitor_);
  if (dev) {
    const char* action = udev_device_get_action(dev);
    const char* sysname = udev_device_get_sysname(dev);
    LOG(INFO) << "Event on ("
              << udev_device_get_subsystem(dev)
              << ") Action "
              << udev_device_get_action(dev)
              << " sys name "
              << udev_device_get_sysname(dev);
    if (strncmp(kEventBaseName, sysname, strlen(kEventBaseName)) == 0) {
      if (strcmp("add", action) == 0)
        AddEvent(sysname);
      else if (strcmp("remove", action) == 0)
        RemoveEvent(sysname);
    } else if (strncmp(kInputBaseName, sysname, strlen(kInputBaseName)) == 0) {
      if (strcmp("add", action) == 0)
        AddWakeInput(sysname);
      else if (strcmp("remove", action) == 0)
        RemoveWakeInput(sysname);
    }
    udev_device_unref(dev);
  } else {
    LOG(ERROR) << "Can't get receive_device()";
    return false;
  }
  return true;
}

void Input::RegisterUdevEventHandler() {
  // Create the udev object.
  udev_ = udev_new();
  if (!udev_)
    LOG(ERROR) << "Can't create udev object";

  // Create the udev monitor structure.
  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    LOG(ERROR) << "Can't create udev monitor";
    udev_unref(udev_);
  }
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_,
                                                  kInputUdevSubsystem,
                                                  NULL);
  udev_monitor_enable_receiving(udev_monitor_);

  int fd = udev_monitor_get_fd(udev_monitor_);

  GIOChannel* channel = g_io_channel_unix_new(fd);
  g_io_add_watch(channel, G_IO_IN, &Input::HandleUdevEventThunk, this);

  LOG(INFO) << "Udev controller waiting for events on subsystem "
            << kInputUdevSubsystem;
}

gboolean Input::HandleInputEvent(GIOChannel* source, GIOCondition condition) {
  if (condition != G_IO_IN)
    return false;
  struct input_event events[64];
  gint fd = g_io_channel_unix_get_fd(source);
  ssize_t read_size = read(fd, events, sizeof(events));
  if (read_size < 0) {
    PLOG(ERROR) << "Read failed in Input::EventHandler";
    return true;
  }
  ssize_t num_events = read_size / sizeof(struct input_event);
  if (num_events < 1) {
    LOG(ERROR) << "Short read in Input::EventHandler";
    return true;
  }
  if ((num_events * sizeof(struct input_event)) !=
      static_cast<size_t>(read_size)) {
    LOG(WARNING) << "Read size " << read_size << " doesn't match expected size "
                 << "for " << num_events << " events in Input::EventHandler";
  }

  for (ssize_t i = 0; i < num_events; i++) {
    InputType input_type = GetInputType(events[i]);
    switch (input_type) {
      case INPUT_LID: {
        LidState state = events[i].value == 1 ? LID_CLOSED : LID_OPEN;
        FOR_EACH_OBSERVER(InputObserver, observers_, OnLidEvent(state));
        break;
      }
      case INPUT_POWER_BUTTON: {
        ButtonState state = BUTTON_DOWN;
        switch (events[i].value) {
          case 0: state = BUTTON_UP;      break;
          case 1: state = BUTTON_DOWN;    break;
          case 2: state = BUTTON_REPEAT;  break;
          default: LOG(ERROR) << "Unhandled button state " << events[i].value;
        }
        FOR_EACH_OBSERVER(InputObserver, observers_, OnPowerButtonEvent(state));
      }
      case INPUT_UNHANDLED:
        break;
    }
  }
  return true;
}

}  // namespace system
}  // namespace power_manager
