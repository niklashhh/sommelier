//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// THIS CODE IS GENERATED.

#include "tpm_manager/common/print_dbus_interface_proto.h"

#include <string>

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "tpm_manager/common/print_local_data_proto.h"

namespace tpm_manager {

std::string GetProtoDebugString(TpmManagerStatus value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(TpmManagerStatus value,
                                          int indent_size) {
  if (value == STATUS_SUCCESS) {
    return "STATUS_SUCCESS";
  }
  if (value == STATUS_UNEXPECTED_DEVICE_ERROR) {
    return "STATUS_UNEXPECTED_DEVICE_ERROR";
  }
  if (value == STATUS_NOT_AVAILABLE) {
    return "STATUS_NOT_AVAILABLE";
  }
  return "<unknown>";
}

std::string GetProtoDebugString(const GetTpmStatusRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetTpmStatusRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetTpmStatusReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetTpmStatusReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_enabled()) {
    output += indent + "  enabled: ";
    base::StringAppendF(&output, "%s", value.enabled() ? "true" : "false");
    output += "\n";
  }
  if (value.has_owned()) {
    output += indent + "  owned: ";
    base::StringAppendF(&output, "%s", value.owned() ? "true" : "false");
    output += "\n";
  }
  if (value.has_local_data()) {
    output += indent + "  local_data: ";
    base::StringAppendF(&output, "%s", GetProtoDebugStringWithIndent(
                                           value.local_data(), indent_size + 2)
                                           .c_str());
    output += "\n";
  }
  if (value.has_dictionary_attack_counter()) {
    output += indent + "  dictionary_attack_counter: ";
    base::StringAppendF(&output, "%d", value.dictionary_attack_counter());
    output += "\n";
  }
  if (value.has_dictionary_attack_threshold()) {
    output += indent + "  dictionary_attack_threshold: ";
    base::StringAppendF(&output, "%d", value.dictionary_attack_threshold());
    output += "\n";
  }
  if (value.has_dictionary_attack_lockout_in_effect()) {
    output += indent + "  dictionary_attack_lockout_in_effect: ";
    base::StringAppendF(
        &output, "%s",
        value.dictionary_attack_lockout_in_effect() ? "true" : "false");
    output += "\n";
  }
  if (value.has_dictionary_attack_lockout_seconds_remaining()) {
    output += indent + "  dictionary_attack_lockout_seconds_remaining: ";
    base::StringAppendF(&output, "%d",
                        value.dictionary_attack_lockout_seconds_remaining());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const TakeOwnershipRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const TakeOwnershipRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const TakeOwnershipReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const TakeOwnershipReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DefineNvramRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DefineNvramRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  if (value.has_length()) {
    output += indent + "  length: ";
    base::StringAppendF(&output, "%d", value.length());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DefineNvramReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DefineNvramReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DestroyNvramRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DestroyNvramRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const DestroyNvramReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const DestroyNvramReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const WriteNvramRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const WriteNvramRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  if (value.has_data()) {
    output += indent + "  data: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.data().data(), value.data().size()).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const WriteNvramReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const WriteNvramReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ReadNvramRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const ReadNvramRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const ReadNvramReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const ReadNvramReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_data()) {
    output += indent + "  data: ";
    base::StringAppendF(
        &output, "%s",
        base::HexEncode(value.data().data(), value.data().size()).c_str());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const IsNvramDefinedRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const IsNvramDefinedRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const IsNvramDefinedReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const IsNvramDefinedReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_is_defined()) {
    output += indent + "  is_defined: ";
    base::StringAppendF(&output, "%s", value.is_defined() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const IsNvramLockedRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const IsNvramLockedRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const IsNvramLockedReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const IsNvramLockedReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_is_locked()) {
    output += indent + "  is_locked: ";
    base::StringAppendF(&output, "%s", value.is_locked() ? "true" : "false");
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetNvramSizeRequest& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetNvramSizeRequest& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_index()) {
    output += indent + "  index: ";
    base::StringAppendF(&output, "%d", value.index());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

std::string GetProtoDebugString(const GetNvramSizeReply& value) {
  return GetProtoDebugStringWithIndent(value, 0);
}

std::string GetProtoDebugStringWithIndent(const GetNvramSizeReply& value,
                                          int indent_size) {
  std::string indent(indent_size, ' ');
  std::string output =
      base::StringPrintf("[%s] {\n", value.GetTypeName().c_str());

  if (value.has_status()) {
    output += indent + "  status: ";
    base::StringAppendF(
        &output, "%s",
        GetProtoDebugStringWithIndent(value.status(), indent_size + 2).c_str());
    output += "\n";
  }
  if (value.has_size()) {
    output += indent + "  size: ";
    base::StringAppendF(&output, "%d", value.size());
    output += "\n";
  }
  output += indent + "}\n";
  return output;
}

}  // namespace tpm_manager
