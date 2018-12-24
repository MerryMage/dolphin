// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/Jit64/RegCache/FPURegCache.h"

#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/Jit64Common/Jit64Base.h"
#include "Core/PowerPC/Jit64Common/Jit64PowerPCState.h"

using namespace Gen;

FPURegCache::FPURegCache(Jit64& jit) : RegCache{jit}
{
}

void FPURegCache::StoreRegister(preg_t preg, const OpArg& new_loc)
{
  switch (m_regs[preg].GetRepr())
  {
  case RCRepr::PairSingles:
    m_emitter->CVTPS2PD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
    [[fallthrough]];
  case RCRepr::Canonical:
    m_emitter->MOVAPD(new_loc, m_regs[preg].Location().GetSimpleReg());
    break;
  case RCRepr::DupSingles:
    m_emitter->CVTSS2SD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
    [[fallthrough]];
  case RCRepr::Dup:
    OpArg second = new_loc;
    second.AddMemOffset(sizeof(double));

    m_emitter->MOVSD(new_loc, m_regs[preg].Location().GetSimpleReg());
    m_emitter->MOVSD(second, m_regs[preg].Location().GetSimpleReg());
    break;
  }
  m_regs[preg].SetRepr(RCRepr::Canonical);
}

void FPURegCache::LoadRegister(preg_t preg, X64Reg new_loc)
{
  m_emitter->MOVAPD(new_loc, m_regs[preg].Location());
}

void FPURegCache::ConvertRegister(preg_t preg, RCRepr new_repr)
{
  switch (new_repr)
  {
  case RCRepr::Canonical:
    switch (m_regs[preg].GetRepr())
    {
    case RCRepr::Canonical:
      // No conversion required.
      break;
    case RCRepr::Dup:
      m_emitter->MOVDDUP(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      break;
    case RCRepr::PairSingles:
      m_emitter->CVTPS2PD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      break;
    case RCRepr::DupSingles:
      m_emitter->CVTSS2SD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      m_emitter->MOVDDUP(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      break;
    }
    m_regs[preg].SetRepr(RCRepr::Canonical);
    break;
  case RCRepr::Dup:
    switch (m_regs[preg].GetRepr())
    {
    case RCRepr::Canonical:
    case RCRepr::Dup:
      // No conversion required.
      break;
    case RCRepr::PairSingles:
      m_emitter->CVTPS2PD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      m_regs[preg].SetRepr(RCRepr::Canonical);
      break;
    case RCRepr::DupSingles:
      m_emitter->CVTSS2SD(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      m_regs[preg].SetRepr(RCRepr::Dup);
      break;
    }
    break;
  case RCRepr::PairSingles:
    switch (m_regs[preg].GetRepr())
    {
    case RCRepr::Canonical:
    case RCRepr::Dup:
      ASSERT_MSG(DYNA_REC, false, "Lossy conversion not allowed");
      break;
    case RCRepr::PairSingles:
      // No conversion required
      break;
    case RCRepr::DupSingles:
      m_emitter->MOVSLDUP(m_regs[preg].Location().GetSimpleReg(), m_regs[preg].Location());
      m_regs[preg].SetRepr(RCRepr::PairSingles);
      break;
    }
  case RCRepr::DupSingles:
    switch (m_regs[preg].GetRepr())
    {
    case RCRepr::Canonical:
    case RCRepr::Dup:
      ASSERT_MSG(DYNA_REC, false, "Lossy conversion not allowed");
      break;
    case RCRepr::PairSingles:
    case RCRepr::DupSingles:
      // No conversion required
      break;
    }
  }
}

const X64Reg* FPURegCache::GetAllocationOrder(size_t* count) const
{
  static const X64Reg allocation_order[] = {XMM6,  XMM7,  XMM8,  XMM9, XMM10, XMM11, XMM12,
                                            XMM13, XMM14, XMM15, XMM2, XMM3,  XMM4,  XMM5};
  *count = sizeof(allocation_order) / sizeof(X64Reg);
  return allocation_order;
}

OpArg FPURegCache::GetDefaultLocation(preg_t preg) const
{
  return PPCSTATE(ps[preg][0]);
}

BitSet32 FPURegCache::GetRegUtilization() const
{
  return m_jit.js.op->gprInReg;
}

BitSet32 FPURegCache::CountRegsIn(preg_t preg, u32 lookahead) const
{
  BitSet32 regs_used;

  for (u32 i = 1; i < lookahead; i++)
  {
    BitSet32 regs_in = m_jit.js.op[i].fregsIn;
    regs_used |= regs_in;
    if (regs_in[preg])
      return regs_used;
  }

  return regs_used;
}
