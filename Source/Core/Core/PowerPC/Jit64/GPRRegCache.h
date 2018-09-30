// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Core/PowerPC/Jit64/JitRegCache.h"

class Jit64;

class GPRRegCache final : public RegCache
{
public:
  explicit GPRRegCache(Jit64& jit);
  Gen::OpArg GetDefaultLocation(size_t reg) const override;
  void SetImmediate32(size_t preg, u32 imm_value, bool dirty = true);

protected:
  void StoreRegister(size_t preg, const Gen::OpArg& new_loc, RegRep src_rep) override;
  void LoadRegister(size_t preg, Gen::X64Reg new_loc, RegRep dest_rep) override;
  void Convert(Gen::X64Reg loc, RegRep src_rep, RegRep dest_rep) override;
  const Gen::X64Reg* GetAllocationOrder(size_t* count) const override;
  BitSet32 GetRegUtilization() const override;
  BitSet32 CountRegsIn(size_t preg, u32 lookahead) const override;
};
