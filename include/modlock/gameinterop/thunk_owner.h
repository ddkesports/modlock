#pragma once

#include "modlock/export.h"
#include "modlock/gameinterop/vtable_slot_hook.h"

namespace modlock::gameinterop {

// ThunkOwner restores a patched dispatch entry before clearing its borrowed
// callback state. Moving transfers both duties; a moved-from owner is inert.
// Install, callbacks and destruction must follow the hook's threading contract.
class MODLOCK_API ThunkOwner {
 public:
  // ClearThunk releases the callback state after restoring the original slot.
  using ClearThunk = void (*)();

  // Takes ownership of the live slot and its optional cleanup callback.
  ThunkOwner(VtableSlotHook slot_hook, ClearThunk clear_thunk);

  ThunkOwner(ThunkOwner&& other) noexcept;
  ThunkOwner& operator=(ThunkOwner&& other) noexcept;
  ~ThunkOwner();

  ThunkOwner(const ThunkOwner&) = delete;
  ThunkOwner& operator=(const ThunkOwner&) = delete;

 private:
  void Reset();

  VtableSlotHook slot_hook_;
  ClearThunk clear_thunk_;
};

}  // namespace modlock::gameinterop
