// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/Jit64Common/BlockCache.h"

#include "Common/CommonTypes.h"
#include "Common/x64Emitter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"

#include "Core/PowerPC/Jit64/RegCache/GPRRegCache.h"
#include "Core/PowerPC/Jit64/RegCache/FPURegCache.h"

#include "Core/PowerPC/Jit64Common/Jit64Base.h"
#include "Core/PowerPC/Jit64Common/Jit64PowerPCState.h"

JitBlockCache::JitBlockCache(JitBase& jit) : JitBaseBlockCache{jit}
{
}

void JitBlockCache::WriteLinkBlock(const JitBlock::LinkData& source, const JitBlock* dest)
{
  u8* location = source.exitPtrs;
  Gen::XEmitter emit(location);

  if (!m_jit.jo.register_handover) {
    const u8* address = dest ? dest->checkedEntry : m_jit.GetAsmRoutines()->dispatcher;

    if (*location == 0xE8)
    {
      emit.CALL(address);
    }
    else
    {
      // If we're going to link with the next block, there is no need
      // to emit JMP. So just NOP out the gap to the next block.
      // Support up to 3 additional bytes because of alignment.
      s64 offset = address - emit.GetCodePtr();
      if (offset > 0 && offset <= 5 + 3)
        emit.NOP(offset);
      else
        emit.JMP(address, true);
    }
    return;
  }

  //m_jit.WriteRegisterHandover(emit, source.unmapped_gpr, source.unmapped_fpr, source.call, dest);

  auto& unmapped_gpr = source.unmapped_gprs;
  auto& unmapped_fpr = source.unmapped_fprs;

  const auto flush_gpr = [&](size_t preg) {
    if (const size_t* xreg = std::get_if<size_t>(&unmapped_gpr[preg]))
    {
      emit.MOV(32, PPCSTATE(gpr[preg]), R(static_cast<Gen::X64Reg>(*xreg)));
    }
    else if (const u32* imm = std::get_if<u32>(&unmapped_gpr[preg]))
    {
      emit.MOV(32, PPCSTATE(gpr[preg]), Gen::Imm32(*imm));
    }
  };

  const auto flush_fpr = [&](size_t preg) {
    ASSERT(!std::get_if<u32>(&unmapped_fpr[preg]));
    if (const size_t* xreg = std::get_if<size_t>(&unmapped_fpr[preg]))
    {
      emit.MOVAPD(PPCSTATE(ps[preg][0]), static_cast<Gen::X64Reg>(*xreg));
    }
  };

  //if (!dest)
  {
    auto bl = source.call;

    for (size_t preg = 0; preg < unmapped_gpr.size(); preg++)
      flush_gpr(preg);
    for (size_t preg = 0; preg < unmapped_fpr.size(); preg++)
      flush_fpr(preg);

    const u8* address = dest ? source.needs_timing_check ? dest->checkedEntry : dest->normalEntry : m_jit.GetAsmRoutines()->dispatcher;
    if (bl)
      emit.CALL(address);
    else
      emit.JMP(address, true);
  }
  /*else
  {
    const auto end_of_handover = std::find(handover_info.begin(), handover_info.end(), [](const auto& info) {
      size_t preg = end_of_handover->preg;
      const auto& loc = preg < 32 ? source.unmapped_gprs[preg] : source.unmapped_fprs[preg - 32];
      return std::holds_alternative<std::monostate>(loc);
    });

    std::set<size_t> handover_regs;
    std::transform(handover_info.begin(), end_of_handover, std::inserter(handover_regs, handover_regs.begin()), [](const auto& info) { return info.preg; });

    for (size_t preg = 0; preg < unmapped_gpr.size(); preg++)
      if (handover_regs.count(preg) == 0)
        flush_gpr(preg);
    for (size_t preg = 0; preg < unmapped_fpr.size(); preg++)
      if (handover_regs.count(preg + 32) == 0)
        flush_fpr(preg);

    std::array<int, 16> gpr_dest, fpr_dest;
    gpr_dest.fill(-1);
    fpr_dest.fill(-1);
    for (auto index = 0, iter = handover_info.begin(); iter != end_of_handover; index++, iter++)
    {
      size_t preg = iter->preg;
      if (preg < 32)
      {
        gpr_dest[source.unmapped_gpr[preg]] = static_cast<int>(gpr.GetHandoverRegister(index));
      }
      else
      {
        fpr_dest[source.unmapped_fpr[preg - 32]] = static_cast<int>(fpr.GetHandoverRegister(index));
      }
    }

loop1:
    for (size_t i = 0; i < gpr_dest.size(); i++)
    {

    }
  }*/

  ASSERT(source.exit_end_ptr >= emit.GetWritableCodePtr());
}

void JitBlockCache::WriteDestroyBlock(const JitBlock& block)
{
  // Only clear the entry points as we might still be within this block.
  Gen::XEmitter emit(block.checkedEntry);
  emit.INT3();
  Gen::XEmitter emit2(block.normalEntry);
  emit2.INT3();
}

std::vector<JitBlock::HandoverInfo> WriteReceiveRegisterHandover(Gen::XEmitter& emit, GPRRegCache& gpr, FPURegCache& fpr, const std::vector<s8>& in)
{
  std::vector<JitBlock::HandoverInfo> info;
  for (s8 i : in)
  {
    const size_t index = info.size();

    if (index >= 8)
      break;

    if (i < 32)
    {
      gpr.PreloadForHandover(index, i);
    }
    else
    {
      fpr.PreloadForHandover(index, i - 32);
    }

    info.push_back({index, i, emit.GetWritableCodePtr()});
  }
  return info;
}
