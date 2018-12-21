// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/Jit64Common/BlockCache.h"

#include "Common/CommonTypes.h"
#include "Common/x64Emitter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"

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
  auto bl = source.call;

  for (size_t preg = 0; preg < unmapped_gpr.size(); preg++)
  {
    if (const size_t* xreg = std::get_if<size_t>(&unmapped_gpr[preg]))
    {
      emit.MOV(32, PPCSTATE(gpr[preg]), R(static_cast<Gen::X64Reg>(*xreg)));
    }
    else if (const u32* imm = std::get_if<u32>(&unmapped_gpr[preg]))
    {
      emit.MOV(32, PPCSTATE(gpr[preg]), Gen::Imm32(*imm));
    }
  }

  for (size_t preg = 0; preg < unmapped_fpr.size(); preg++)
  {
    ASSERT(!std::get_if<u32>(&unmapped_fpr[preg]));
    if (const size_t* xreg = std::get_if<size_t>(&unmapped_fpr[preg]))
    {
      emit.MOVAPD(PPCSTATE(ps[preg][0]), static_cast<Gen::X64Reg>(*xreg));
    }
  }

  const u8* address = dest ? dest->checkedEntry : m_jit.GetAsmRoutines()->dispatcher;
  if (bl)
    emit.CALL(address);
  else
    emit.JMP(address, true);

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
