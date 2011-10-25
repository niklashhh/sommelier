// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/chaps_proxy.h"

#include <base/logging.h>

#include "chaps/chaps.h"
#include "chaps/chaps_utility.h"
#include "pkcs11/cryptoki.h"

using std::string;
using std::vector;

namespace chaps {

ChapsProxyImpl::ChapsProxyImpl() {}

ChapsProxyImpl::~ChapsProxyImpl() {}

bool ChapsProxyImpl::Init() {
  try {
    if (!DBus::default_dispatcher)
      DBus::default_dispatcher = new DBus::BusDispatcher();
    if (!proxy_.get()) {
      // Establish a D-Bus connection.
      DBus::Connection connection = DBus::Connection::SystemBus();
      proxy_.reset(new Proxy(connection, kChapsServicePath,
                             kChapsServiceName));
    }
    if (proxy_.get())
      return true;
  } catch (DBus::Error err) {
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return false;
}

uint32_t ChapsProxyImpl::GetSlotList(bool token_present,
                                     vector<uint32_t>* slot_list) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!slot_list, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetSlotList(token_present, *slot_list, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetSlotInfo(uint32_t slot_id,
                                     string* slot_description,
                                     string* manufacturer_id,
                                     uint32_t* flags,
                                     uint8_t* hardware_version_major,
                                     uint8_t* hardware_version_minor,
                                     uint8_t* firmware_version_major,
                                     uint8_t* firmware_version_minor) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  if (!slot_description || !manufacturer_id || !flags ||
      !hardware_version_major || !hardware_version_minor ||
      !firmware_version_major || !firmware_version_minor)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetSlotInfo(slot_id,
                        *slot_description,
                        *manufacturer_id,
                        *flags,
                        *hardware_version_major,
                        *hardware_version_minor,
                        *firmware_version_major,
                        *firmware_version_minor,
                        result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetTokenInfo(uint32_t slot_id,
                                      string* label,
                                      string* manufacturer_id,
                                      string* model,
                                      string* serial_number,
                                      uint32_t* flags,
                                      uint32_t* max_session_count,
                                      uint32_t* session_count,
                                      uint32_t* max_session_count_rw,
                                      uint32_t* session_count_rw,
                                      uint32_t* max_pin_len,
                                      uint32_t* min_pin_len,
                                      uint32_t* total_public_memory,
                                      uint32_t* free_public_memory,
                                      uint32_t* total_private_memory,
                                      uint32_t* free_private_memory,
                                      uint8_t* hardware_version_major,
                                      uint8_t* hardware_version_minor,
                                      uint8_t* firmware_version_major,
                                      uint8_t* firmware_version_minor) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  if (!label || !manufacturer_id || !model || !serial_number || !flags ||
      !max_session_count || !session_count || !max_session_count_rw ||
      !session_count_rw || !max_pin_len || !min_pin_len ||
      !total_public_memory || !free_public_memory || !total_private_memory ||
      !total_public_memory ||
      !hardware_version_major || !hardware_version_minor ||
      !firmware_version_major || !firmware_version_minor)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetTokenInfo(slot_id,
                         *label,
                         *manufacturer_id,
                         *model,
                         *serial_number,
                         *flags,
                         *max_session_count,
                         *session_count,
                         *max_session_count_rw,
                         *session_count_rw,
                         *max_pin_len,
                         *min_pin_len,
                         *total_public_memory,
                         *free_public_memory,
                         *total_private_memory,
                         *free_private_memory,
                         *hardware_version_major,
                         *hardware_version_minor,
                         *firmware_version_major,
                         *firmware_version_minor,
                         result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetMechanismList(
    uint32_t slot_id,
    vector<uint32_t>* mechanism_list) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!mechanism_list, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetMechanismList(slot_id, *mechanism_list, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetMechanismInfo(uint32_t slot_id,
                                          uint32_t mechanism_type,
                                          uint32_t* min_key_size,
                                          uint32_t* max_key_size,
                                          uint32_t* flags) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  if (!min_key_size || !max_key_size || !flags)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetMechanismInfo(slot_id,
                             mechanism_type,
                             *min_key_size,
                             *max_key_size,
                             *flags,
                             result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::InitToken(uint32_t slot_id, const string* so_pin,
                                   const string& label) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    string tmp_pin;
    if (so_pin)
      tmp_pin = *so_pin;
    result = proxy_->InitToken(slot_id, (so_pin == NULL), tmp_pin, label);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::InitPIN(uint32_t session_id, const string* pin) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    string tmp_pin;
    if (pin)
      tmp_pin = *pin;
    result = proxy_->InitPIN(session_id, (pin == NULL), tmp_pin);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SetPIN(uint32_t session_id, const string* old_pin,
                                const string* new_pin) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    string tmp_old_pin;
    if (old_pin)
      tmp_old_pin = *old_pin;
    string tmp_new_pin;
    if (new_pin)
      tmp_new_pin = *new_pin;
    result = proxy_->SetPIN(session_id, (old_pin == NULL), tmp_old_pin,
                            (new_pin == NULL), tmp_new_pin);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::OpenSession(uint32_t slot_id, uint32_t flags,
                                     uint32_t* session_id) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!session_id, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->OpenSession(slot_id, flags, *session_id, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::CloseSession(uint32_t session_id) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->CloseSession(session_id);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::CloseAllSessions(uint32_t slot_id) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->CloseAllSessions(slot_id);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetSessionInfo(uint32_t session_id,
                                        uint32_t* slot_id,
                                        uint32_t* state,
                                        uint32_t* flags,
                                        uint32_t* device_error) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  if (!slot_id || !state || !flags || !device_error)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetSessionInfo(session_id, *slot_id, *state, *flags, *device_error,
                           result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetOperationState(uint32_t session_id,
                                           vector<uint8_t>* operation_state) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!operation_state, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetOperationState(session_id, *operation_state, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SetOperationState(
    uint32_t session_id,
    const vector<uint8_t>& operation_state,
    uint32_t encryption_key_handle,
    uint32_t authentication_key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->SetOperationState(session_id,
                                       operation_state,
                                       encryption_key_handle,
                                       authentication_key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Login(uint32_t session_id,
                               uint32_t user_type,
                               const string* pin) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    string tmp_pin;
    if (pin)
      tmp_pin = *pin;
    result = proxy_->Login(session_id, user_type, (pin == NULL), tmp_pin);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Logout(uint32_t session_id) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->Logout(session_id);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::CreateObject(uint32_t session_id,
                                      const vector<uint8_t>& attributes,
                                      uint32_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->CreateObject(session_id,
                         attributes,
                         *new_object_handle,
                         result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::CopyObject(uint32_t session_id,
                                    uint32_t object_handle,
                                    const vector<uint8_t>& attributes,
                                    uint32_t* new_object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!new_object_handle, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->CopyObject(session_id,
                       object_handle,
                       attributes,
                       *new_object_handle,
                       result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DestroyObject(uint32_t session_id,
                                       uint32_t object_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->DestroyObject(session_id, object_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetObjectSize(uint32_t session_id,
                                       uint32_t object_handle,
                                       uint32_t* object_size) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!object_size, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetObjectSize(session_id, object_handle, *object_size, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::GetAttributeValue(uint32_t session_id,
                                           uint32_t object_handle,
                                           const vector<uint8_t>& attributes_in,
                                           vector<uint8_t>* attributes_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!attributes_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->GetAttributeValue(session_id,
                              object_handle,
                              attributes_in,
                              *attributes_out,
                              result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SetAttributeValue(uint32_t session_id,
                                           uint32_t object_handle,
                                           const vector<uint8_t>& attributes) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->SetAttributeValue(session_id,
                                       object_handle,
                                       attributes);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::FindObjectsInit(
    uint32_t session_id,
    const vector<uint8_t>& attributes) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->FindObjectsInit(session_id, attributes);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::FindObjects(uint32_t session_id,
                                     uint32_t max_object_count,
                                     vector<uint32_t>* object_list) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  if (!object_list || object_list->size() > 0)
    LOG_CK_RV_AND_RETURN(CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->FindObjects(session_id, max_object_count, *object_list, result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::FindObjectsFinal(uint32_t session_id) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->FindObjectsFinal(session_id);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::EncryptInit(
    uint32_t session_id,
    uint32_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->EncryptInit(session_id,
                                 mechanism_type,
                                 mechanism_parameter,
                                 key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Encrypt(uint32_t session_id,
                                 const vector<uint8_t>& data_in,
                                 uint32_t max_out_length,
                                 uint32_t* actual_out_length,
                                 vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->Encrypt(session_id,
                    data_in,
                    max_out_length,
                    *actual_out_length,
                    *data_out,
                    result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::EncryptUpdate(uint32_t session_id,
                                       const vector<uint8_t>& data_in,
                                       uint32_t max_out_length,
                                       uint32_t* actual_out_length,
                                       vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->EncryptUpdate(session_id,
                          data_in,
                          max_out_length,
                          *actual_out_length,
                          *data_out,
                          result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::EncryptFinal(uint32_t session_id,
                                      uint32_t max_out_length,
                                      uint32_t* actual_out_length,
                                      vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->EncryptFinal(session_id,
                         max_out_length,
                         *actual_out_length,
                         *data_out,
                         result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptInit(
    uint32_t session_id,
    uint32_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter,
    uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->DecryptInit(session_id,
                                 mechanism_type,
                                 mechanism_parameter,
                                 key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Decrypt(uint32_t session_id,
                                 const vector<uint8_t>& data_in,
                                 uint32_t max_out_length,
                                 uint32_t* actual_out_length,
                                 vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->Decrypt(session_id,
                    data_in,
                    max_out_length,
                    *actual_out_length,
                    *data_out,
                    result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptUpdate(uint32_t session_id,
                                       const vector<uint8_t>& data_in,
                                       uint32_t max_out_length,
                                       uint32_t* actual_out_length,
                                       vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DecryptUpdate(session_id,
                          data_in,
                          max_out_length,
                          *actual_out_length,
                          *data_out,
                          result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptFinal(uint32_t session_id,
                                      uint32_t max_out_length,
                                      uint32_t* actual_out_length,
                                      vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DecryptFinal(session_id,
                         max_out_length,
                         *actual_out_length,
                         *data_out,
                         result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DigestInit(
    uint32_t session_id,
    uint32_t mechanism_type,
    const vector<uint8_t>& mechanism_parameter) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->DigestInit(session_id,
                                mechanism_type,
                                mechanism_parameter);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Digest(uint32_t session_id,
                                const vector<uint8_t>& data_in,
                                uint32_t max_out_length,
                                uint32_t* actual_out_length,
                                vector<uint8_t>* digest) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->Digest(session_id,
                   data_in,
                   max_out_length,
                   *actual_out_length,
                   *digest,
                   result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DigestUpdate(uint32_t session_id,
                                      const vector<uint8_t>& data_in) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->DigestUpdate(session_id, data_in);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DigestKey(uint32_t session_id,
                                   uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->DigestKey(session_id, key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DigestFinal(uint32_t session_id,
                                     uint32_t max_out_length,
                                     uint32_t* actual_out_length,
                                     vector<uint8_t>* digest) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DigestFinal(session_id,
                        max_out_length,
                        *actual_out_length,
                        *digest,
                        result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignInit(uint32_t session_id,
                                  uint32_t mechanism_type,
                                  const vector<uint8_t>& mechanism_parameter,
                                  uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->SignInit(session_id,
                              mechanism_type,
                              mechanism_parameter,
                              key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Sign(uint32_t session_id,
                              const vector<uint8_t>& data,
                              uint32_t max_out_length,
                              uint32_t* actual_out_length,
                              vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->Sign(session_id,
                 data,
                 max_out_length,
                 *actual_out_length,
                 *signature,
                 result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignUpdate(uint32_t session_id,
                                    const vector<uint8_t>& data_part) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->SignUpdate(session_id, data_part);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignFinal(uint32_t session_id,
                                   uint32_t max_out_length,
                                   uint32_t* actual_out_length,
                                   vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->SignFinal(session_id,
                      max_out_length,
                      *actual_out_length,
                      *signature,
                      result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignRecoverInit(
      uint32_t session_id,
      uint32_t mechanism_type,
      const vector<uint8_t>& mechanism_parameter,
      uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->SignRecoverInit(session_id,
                                     mechanism_type,
                                     mechanism_parameter,
                                     key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignRecover(uint32_t session_id,
                                     const vector<uint8_t>& data,
                                     uint32_t max_out_length,
                                     uint32_t* actual_out_length,
                                     vector<uint8_t>* signature) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->SignRecover(session_id,
                        data,
                        max_out_length,
                        *actual_out_length,
                        *signature,
                        result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::VerifyInit(uint32_t session_id,
                                    uint32_t mechanism_type,
                                    const vector<uint8_t>& mechanism_parameter,
                                    uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->VerifyInit(session_id,
                                mechanism_type,
                                mechanism_parameter,
                                key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::Verify(uint32_t session_id,
                                const vector<uint8_t>& data,
                                const vector<uint8_t>& signature) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->Verify(session_id, data, signature);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::VerifyUpdate(uint32_t session_id,
                                      const vector<uint8_t>& data_part) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->VerifyUpdate(session_id, data_part);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::VerifyFinal(uint32_t session_id,
                                     const vector<uint8_t>& signature) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->VerifyFinal(session_id, signature);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::VerifyRecoverInit(
      uint32_t session_id,
      uint32_t mechanism_type,
      const vector<uint8_t>& mechanism_parameter,
      uint32_t key_handle) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    result = proxy_->VerifyRecoverInit(session_id,
                                       mechanism_type,
                                       mechanism_parameter,
                                       key_handle);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::VerifyRecover(uint32_t session_id,
                                       const vector<uint8_t>& signature,
                                       uint32_t max_out_length,
                                       uint32_t* actual_out_length,
                                       vector<uint8_t>* data) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->VerifyRecover(session_id,
                          signature,
                          max_out_length,
                          *actual_out_length,
                          *data,
                          result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DigestEncryptUpdate(
    uint32_t session_id,
    const vector<uint8_t>& data_in,
    uint32_t max_out_length,
    uint32_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DigestEncryptUpdate(session_id,
                                data_in,
                                max_out_length,
                                *actual_out_length,
                                *data_out,
                                result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptDigestUpdate(
    uint32_t session_id,
    const vector<uint8_t>& data_in,
    uint32_t max_out_length,
    uint32_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DecryptDigestUpdate(session_id,
                                data_in,
                                max_out_length,
                                *actual_out_length,
                                *data_out,
                                result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::SignEncryptUpdate(uint32_t session_id,
                                           const vector<uint8_t>& data_in,
                                           uint32_t max_out_length,
                                           uint32_t* actual_out_length,
                                           vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->SignEncryptUpdate(session_id,
                              data_in,
                              max_out_length,
                              *actual_out_length,
                              *data_out,
                              result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

uint32_t ChapsProxyImpl::DecryptVerifyUpdate(
    uint32_t session_id,
    const vector<uint8_t>& data_in,
    uint32_t max_out_length,
    uint32_t* actual_out_length,
    vector<uint8_t>* data_out) {
  LOG_CK_RV_AND_RETURN_IF(!proxy_.get(), CKR_CRYPTOKI_NOT_INITIALIZED);
  LOG_CK_RV_AND_RETURN_IF(!actual_out_length || !data_out, CKR_ARGUMENTS_BAD);
  uint32_t result = CKR_GENERAL_ERROR;
  try {
    proxy_->DecryptVerifyUpdate(session_id,
                                data_in,
                                max_out_length,
                                *actual_out_length,
                                *data_out,
                                result);
  } catch (DBus::Error err) {
    result = CKR_GENERAL_ERROR;
    LOG(ERROR) << "DBus::Error - " << err.what();
  }
  return result;
}

}  // namespace
