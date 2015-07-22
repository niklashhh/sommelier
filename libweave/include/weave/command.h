// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_INCLUDE_WEAVE_COMMAND_H_
#define LIBWEAVE_INCLUDE_WEAVE_COMMAND_H_

#include <string>

#include <base/values.h>

namespace weave {

class Command {
 public:
  // This interface lets the command to notify clients about changes.
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnResultsChanged() = 0;
    virtual void OnStatusChanged() = 0;
    virtual void OnProgressChanged() = 0;
    virtual void OnCommandDestroyed() = 0;
  };

  // Adds an observer for this command. The observer object is not owned by this
  // class.
  virtual void AddObserver(Observer* observer) = 0;

  // Returns the full command ID.
  virtual const std::string& GetID() const = 0;

  // Returns the full name of the command.
  virtual const std::string& GetName() const = 0;

  // Returns the command category.
  virtual const std::string& GetCategory() const = 0;

  // Returns the command status.
  // TODO(vitalybuka): Status should be enum.
  virtual const std::string& GetStatus() const = 0;

  // Returns the origin of the command.
  virtual const std::string& GetOrigin() const = 0;

  // Returns the command parameters.
  virtual std::unique_ptr<base::DictionaryValue> GetParameters() const = 0;

  // Returns the command progress.
  virtual std::unique_ptr<base::DictionaryValue> GetProgress() const = 0;

  // Returns the command results.
  virtual std::unique_ptr<base::DictionaryValue> GetResults() const = 0;

  // Returns JSON representation of the command.
  virtual std::unique_ptr<base::DictionaryValue> ToJson() const = 0;

 protected:
  virtual ~Command() = default;
};

}  // namespace weave

#endif  // LIBWEAVE_INCLUDE_WEAVE_COMMAND_H_
