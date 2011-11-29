// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_RESOLVER_
#define SHILL_RESOLVER_

#include <string>
#include <vector>

#include <base/file_path.h>
#include <base/lazy_instance.h>
#include <base/memory/ref_counted.h>

#include "shill/refptr_types.h"

namespace shill {

// This provides a static function for dumping the DNS information out
// of an ipconfig into a "resolv.conf" formatted file.
class Resolver {
 public:
  virtual ~Resolver();

  // Since this is a singleton, use Resolver::GetInstance()->Foo()
  static Resolver *GetInstance();

  virtual void set_path(const FilePath &path) { path_ = path; }

  // Set the default domain name service parameters, given an ipconfig entry
  virtual bool SetDNSFromIPConfig(const IPConfigRefPtr &ipconfig);

  // Set the default domain name service parameters, given an ipconfig entry
  virtual bool SetDNSFromLists(const std::vector<std::string> &dns_servers,
                               const std::vector<std::string> &domain_search);

  // Remove any created domain name service file
  virtual bool ClearDNS();

 protected:
  Resolver();

 private:
  friend struct base::DefaultLazyInstanceTraits<Resolver>;
  friend class ResolverTest;

  FilePath path_;

  DISALLOW_COPY_AND_ASSIGN(Resolver);
};

}  // namespace shill

#endif  // SHILL_RESOLVER_
