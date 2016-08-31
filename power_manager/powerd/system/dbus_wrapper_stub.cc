// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/dbus_wrapper_stub.h"

#include <memory>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <dbus/dbus.h>

namespace power_manager {
namespace system {

namespace {

// Returns a copy of |signal|.
std::unique_ptr<dbus::Signal> DuplicateSignal(dbus::Signal* signal) {
  return base::WrapUnique(
      dbus::Signal::FromRawMessage(dbus_message_copy(signal->raw_message())));
}

}  // namespace

DBusWrapperStub::DBusWrapperStub() : service_published_(false) {}

DBusWrapperStub::~DBusWrapperStub() {}

bool DBusWrapperStub::GetSentSignal(size_t index,
                                    const std::string& expected_signal_name,
                                    google::protobuf::MessageLite* protobuf_out,
                                    std::unique_ptr<dbus::Signal>* signal_out) {
  if (index >= sent_signals_.size()) {
    LOG(ERROR) << "Got request to return " << expected_signal_name << " signal "
               << "at position " << index << ", but only "
               << sent_signals_.size() << " were sent";
    return false;
  }

  SignalInfo& info = sent_signals_[index];
  if (info.signal_name != expected_signal_name) {
    LOG(ERROR) << "Expected " << expected_signal_name << " signal at position "
               << index << " but had " << info.signal_name << " instead";
    return false;
  }

  if (protobuf_out) {
    if (info.protobuf_type != protobuf_out->GetTypeName()) {
      LOG(ERROR) << info.signal_name << " signal at position " << index
                 << " has " << info.protobuf_type << " protobuf instead of "
                 << "expected " << protobuf_out->GetTypeName();
      return false;
    }
    if (!protobuf_out->ParseFromString(info.serialized_data)) {
      LOG(ERROR) << "Unable to parse " << info.protobuf_type
                 << " protobuf from " << info.signal_name
                 << " signal at position " << index;
      return false;
    }
  }

  if (signal_out) {
    if (!info.signal.get()) {
      LOG(ERROR) << info.signal_name << " signal at position " << index
                 << " wasn't sent using EmitSignal()";
      return false;
    }
    *signal_out = DuplicateSignal(info.signal.get());
  }

  return true;
}

void DBusWrapperStub::ClearSentSignals() {
  sent_signals_.clear();
}

void DBusWrapperStub::CallExportedMethod(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_cb) {
  CHECK(method_call);

  // libdbus asserts that the serial number is set. Prevent tests from needing
  // to bother setting it.
  method_call->SetSerial(1);

  const std::string name = method_call->GetMember();
  CHECK(exported_methods_.count(name)) << " Method " << name << " not exported";
  exported_methods_[name].Run(method_call, response_cb);
}

dbus::Bus* DBusWrapperStub::GetBus() {
  return nullptr;
}

dbus::ObjectProxy* DBusWrapperStub::GetObjectProxy(
    const std::string& service_name,
    const std::string& object_path) {
  // If a proxy was already created, return it.
  for (const auto& info : object_proxy_infos_) {
    if (info.service_name == service_name &&
        info.object_path == object_path) {
      return info.object_proxy.get();
    }
  }

  // Ownership of this is passed to ObjectProxyInfo in the next statement.
  dbus::ObjectProxy* object_proxy = new dbus::ObjectProxy(
      nullptr, service_name, dbus::ObjectPath(object_path), 0);
  object_proxy_infos_.emplace_back(
      ObjectProxyInfo{service_name, object_path, object_proxy});
  return object_proxy;
}

void DBusWrapperStub::RegisterForServiceAvailability(
    dbus::ObjectProxy* proxy,
    dbus::ObjectProxy::WaitForServiceToBeAvailableCallback callback) {
  DCHECK(proxy);
  // TODO(derat): Record registered services.
}

void DBusWrapperStub::RegisterForSignal(
    dbus::ObjectProxy* proxy,
    const std::string& interface_name,
    const std::string& signal_name,
    dbus::ObjectProxy::SignalCallback callback) {
  DCHECK(proxy);
  // TODO(derat): Record registered signals.
}

void DBusWrapperStub::ExportMethod(
    const std::string& method_name,
    dbus::ExportedObject::MethodCallCallback callback) {
  CHECK(!service_published_) << "Method " << method_name
                             << " exported after service already published";
  CHECK(!exported_methods_.count(method_name)) << "Method " << method_name
                                               << " exported twice";
  exported_methods_[method_name] = callback;
}

bool DBusWrapperStub::PublishService() {
  CHECK(!service_published_) << "Service already published";
  service_published_ = true;
  return true;
}

void DBusWrapperStub::EmitSignal(dbus::Signal* signal) {
  DCHECK(signal);
  sent_signals_.emplace_back(
      SignalInfo{signal->GetMember(), DuplicateSignal(signal)});
}

void DBusWrapperStub::EmitBareSignal(const std::string& signal_name) {
  sent_signals_.emplace_back(SignalInfo{signal_name});
}

void DBusWrapperStub::EmitSignalWithProtocolBuffer(
    const std::string& signal_name,
    const google::protobuf::MessageLite& protobuf) {
  std::string serialized_data;
  protobuf.SerializeToString(&serialized_data);
  sent_signals_.emplace_back(
      SignalInfo{signal_name, std::unique_ptr<dbus::Signal>(),
                 protobuf.GetTypeName(), serialized_data});
}

std::unique_ptr<dbus::Response> DBusWrapperStub::CallMethodSync(
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* method_call,
    base::TimeDelta timeout) {
  DCHECK(proxy);
  DCHECK(method_call);
  // TODO(derat): Return canned response.
  return std::unique_ptr<dbus::Response>();
}

void DBusWrapperStub::CallMethodAsync(
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* method_call,
    base::TimeDelta timeout,
    dbus::ObjectProxy::ResponseCallback callback) {
  DCHECK(proxy);
  DCHECK(method_call);
  // TODO(derat): Invoke callback with canned response.
}

}  // namespace system
}  // namespace power_manager
