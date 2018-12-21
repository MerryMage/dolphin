// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "Core/PowerPC/JitCommon/JitCache.h"

namespace Gen
{
  class XEmitter;
}  // namespace Gen

class FPURegCache;
class GPRRegCache;
class JitBase;

class JitBlockCache : public JitBaseBlockCache
{
public:
  explicit JitBlockCache(JitBase& jit);

private:
  void WriteLinkBlock(const JitBlock::LinkData& source, const JitBlock* dest) override;
  void WriteDestroyBlock(const JitBlock& block) override;
};

std::vector<JitBlock::HandoverInfo> WriteReceiveRegisterHandover(Gen::XEmitter& emit, GPRRegCache& gpr, FPURegCache& fpu, const std::vector<s8>& in);
