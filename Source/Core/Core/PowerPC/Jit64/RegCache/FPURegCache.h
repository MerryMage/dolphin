// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Core/PowerPC/Jit64/RegCache/JitRegCache.h"

class Jit64;

class FPURegCache final : public RegCache
{
public:
  explicit FPURegCache(Jit64& jit);

  template <typename... Args>
  bool IsSingle(Args... pregs) const
  {
    const auto is_single = [this](preg_t preg) {
      switch (m_regs[preg].GetRepr())
      {
      case RCRepr::Canonical:
      case RCRepr::Dup:
        return false;
      case RCRepr::PairSingles:
      case RCRepr::DupSingles:
        return true;
      }
    };

    static_assert(sizeof...(pregs) > 0);
    return (is_single(pregs) && ...);
  }

  template <typename... Args>
  bool IsDup(Args... pregs) const
  {
    const auto is_dup = [this](preg_t preg) {
      switch (m_regs[preg].GetRepr())
      {
      case RCRepr::Canonical:
      case RCRepr::PairSingles:
        return false;
      case RCRepr::Dup:
      case RCRepr::DupSingles:
        return true;
      }
    };

    static_assert(sizeof...(pregs) > 0);
    return (is_dup(pregs) && ...);
  }

protected:
  Gen::OpArg GetDefaultLocation(preg_t preg) const override;
  void StoreRegister(preg_t preg, const Gen::OpArg& newLoc) override;
  void LoadRegister(preg_t preg, Gen::X64Reg newLoc) override;
  void ConvertRegister(preg_t preg, RCRepr new_repr) override;
  const Gen::X64Reg* GetAllocationOrder(size_t* count) const override;
  BitSet32 GetRegUtilization() const override;
  BitSet32 CountRegsIn(preg_t preg, u32 lookahead) const override;
};
