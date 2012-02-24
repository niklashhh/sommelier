// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_PERSISTENCE_MOCK_H
#define CHAPS_PERSISTENCE_MOCK_H

#include "chaps/object_store.h"

namespace chaps {

class ObjectStoreMock : public ObjectStore {
 public:
  MOCK_METHOD2(GetInternalBlob,
      bool(int blob_id, std::string* blob));
  MOCK_METHOD2(SetInternalBlob,
      bool(int blob_id, const std::string& blob));
  MOCK_METHOD1(SetEncryptionKey,
      void(const std::string& key));
  MOCK_METHOD5(InsertObjectBlob,
      bool(bool is_private,
           CK_OBJECT_CLASS object_class,
           const std::string& key_id,
           const std::string& blob,
           int* blob_id));
  MOCK_METHOD1(DeleteObjectBlob,
      bool(int blob_id));
  MOCK_METHOD2(UpdateObjectBlob,
      bool(int blob_id, const std::string& blob));
  MOCK_METHOD1(LoadAllObjectBlobs,
      bool(std::map<int, std::string>* blobs));
};

}  // namespace

#endif  // CHAPS_PERSISTENCE_MOCK_H
