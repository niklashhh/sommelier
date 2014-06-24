// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_PAYLOAD_SIGNER_H_
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_PAYLOAD_SIGNER_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include "update_engine/update_metadata.pb.h"

// This class encapsulates methods used for payload signing and signature
// verification. See update_metadata.proto for more info.

namespace chromeos_update_engine {

extern const uint32_t kSignatureMessageOriginalVersion;
extern const uint32_t kSignatureMessageCurrentVersion;

class PayloadSigner {
 public:
  // Given a raw |hash| and a private key in |private_key_path| calculates the
  // raw signature in |out_signature|. Returns true on success, false otherwise.
  static bool SignHash(const std::vector<char>& hash,
                       const std::string& private_key_path,
                       std::vector<char>* out_signature);

  // Given an unsigned payload in |unsigned_payload_path| and private keys in
  // |private_key_path|, calculates the signature blob into
  // |out_signature_blob|. Note that the payload must already have an updated
  // manifest that includes the dummy signature op. Returns true on success,
  // false otherwise.
  static bool SignPayload(const std::string& unsigned_payload_path,
                          const std::vector<std::string>& private_key_paths,
                          std::vector<char>* out_signature_blob);

  // Returns the length of out_signature_blob that will result in a call
  // to SignPayload with the given private keys. Returns true on success.
  static bool SignatureBlobLength(
      const std::vector<std::string>& private_key_paths,
      uint64_t* out_length);

  // This is a helper method for HashPayloadforSigning and
  // HashMetadataForSigning. It loads the payload into memory, and inserts
  // signature placeholders if Signatures aren't already present.
  static bool PrepPayloadForHashing(
        const std::string& payload_path,
        const std::vector<int>& signature_sizes,
        std::vector<char>* payload_out,
        uint64_t* metadata_size_out,
        uint64_t* signatures_offset_out);

  // Given an unsigned payload in |payload_path|,
  // this method does two things:
  // 1. Uses PrepPayloadForHashing to inserts placeholder signature operations
  //    to make the manifest match what the final signed payload will look
  //    like based on |signatures_sizes|, if needed.
  // 2. It calculates the raw SHA256 hash of the payload in |payload_path|
  //    (except signatures) and returns the result in |out_hash_data|.
  //
  // The dummy signatures are not preserved or written to disk.
  static bool HashPayloadForSigning(const std::string& payload_path,
                                    const std::vector<int>& signature_sizes,
                                    std::vector<char>* out_hash_data);

  // Given an unsigned payload in |payload_path|,
  // this method does two things:
  // 1. Uses PrepPayloadForHashing to inserts placeholder signature operations
  //    to make the manifest match what the final signed payload will look
  //    like based on |signatures_sizes|, if needed.
  // 2. It calculates the raw SHA256 hash of the metadata from the payload in
  //    |payload_path| (except signatures) and returns the result in
  //    |out_metadata_hash|.
  //
  // The dummy signatures are not preserved or written to disk.
  static bool HashMetadataForSigning(const std::string& payload_path,
                                     const std::vector<int>& signature_sizes,
                                     std::vector<char>* out_metadata_hash);

  // Given an unsigned payload in |payload_path| (with no dummy signature op)
  // and the raw |signatures| updates the payload to include the signature thus
  // turning it into a signed payload. The new payload is stored in
  // |signed_payload_path|. |payload_path| and |signed_payload_path| can point
  // to the same file. Populates |out_metadata_size| with the size of the
  // metadata after adding the signature operation in the manifest.Returns true
  // on success, false otherwise.
  static bool AddSignatureToPayload(
      const std::string& payload_path,
      const std::vector<std::vector<char> >& signatures,
      const std::string& signed_payload_path,
      uint64_t* out_metadata_size);

  // Returns false if the payload signature can't be verified. Returns true
  // otherwise and sets |out_hash| to the signed payload hash.
  static bool VerifySignature(const std::vector<char>& signature_blob,
                              const std::string& public_key_path,
                              std::vector<char>* out_hash_data);

  // Interprets signature_blob as a protocol buffer containing the Signatures
  // message and decrypts the signature data using the public_key_path and
  // stores the resultant raw hash data in out_hash_data. Returns true if
  // everything is successful. False otherwise. It also takes the client_version
  // and interprets the signature blob according to that version.
  static bool VerifySignatureBlob(const std::vector<char>& signature_blob,
                                  const std::string& public_key_path,
                                  uint32_t client_version,
                                  std::vector<char>* out_hash_data);

  // Decrypts sig_data with the given public_key_path and populates
  // out_hash_data with the decoded raw hash. Returns true if successful,
  // false otherwise.
  static bool GetRawHashFromSignature(const std::vector<char>& sig_data,
                                      const std::string& public_key_path,
                                      std::vector<char>* out_hash_data);

  // Returns true if the payload in |payload_path| is signed and its hash can be
  // verified using the public key in |public_key_path| with the signature
  // of a given version in the signature blob. Returns false otherwise.
  static bool VerifySignedPayload(const std::string& payload_path,
                                  const std::string& public_key_path,
                                  uint32_t client_key_check_version);

  // Pads a SHA256 hash so that it may be encrypted/signed with RSA2048
  // using the PKCS#1 v1.5 scheme.
  // hash should be a pointer to vector of exactly 256 bits. The vector
  // will be modified in place and will result in having a length of
  // 2048 bits. Returns true on success, false otherwise.
  static bool PadRSA2048SHA256Hash(std::vector<char>* hash);

  // Computes the SHA256 hash of the first metadata_size bytes of |metadata|
  // and signs the hash with the given private_key_path and writes the signed
  // hash in |out_signature|. Returns true if successful or false if there was
  // any error in the computations.
  static bool GetMetadataSignature(const char* const metadata,
                                   size_t metadata_size,
                                   const std::string& private_key_path,
                                   std::string* out_signature);

  // Reads the payload from the given |payload_path| into the |out_payload|
  // vector. It also parses the manifest protobuf in the payload and returns it
  // in |out_manifest| along with the size of the entire metadata in
  // |out_metadata_size|.
  static bool LoadPayload(const std::string& payload_path,
                          std::vector<char>* out_payload,
                          DeltaArchiveManifest* out_manifest,
                          uint64_t* out_metadata_size);

 private:
  // This should never be constructed
  DISALLOW_IMPLICIT_CONSTRUCTORS(PayloadSigner);
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_PAYLOAD_SIGNER_H_
