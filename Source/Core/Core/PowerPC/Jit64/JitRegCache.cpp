// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/PowerPC/Jit64/JitRegCache.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <limits>

#include "Common/Assert.h"
#include "Common/BitSet.h"
#include "Common/CommonTypes.h"
#include "Common/MsgHandler.h"
#include "Common/x64Emitter.h"
#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/PowerPC.h"

using namespace Gen;
using namespace PowerPC;

RegCache::RegCache(Jit64& jit) : m_jit{jit}
{
}

void RegCache::Start()
{
  m_xregs.fill({});
  for (size_t i = 0; i < m_regs.size(); i++)
  {
    m_regs[i] = PPCCachedReg{GetDefaultLocation(i)};
  }
}

void RegCache::DiscardRegContentsIfCached(size_t preg)
{
  if (m_regs[preg].IsBound())
  {
    X64Reg xr = m_regs[preg].Location().GetSimpleReg();
    m_xregs[xr].Flushed();
    m_regs[preg].Flushed();
  }
}

void RegCache::SetEmitter(XEmitter* emitter)
{
  m_emitter = emitter;
}

void RegCache::Flush(FlushMode mode, BitSet32 regsToFlush)
{
  ASSERT_MSG(
      DYNA_REC,
      !std::any_of(m_xregs.begin(), m_xregs.end(), [](const auto& x) { return x.IsLocked(); }),
      "Someone forgot to unlock a X64 reg");

  for (unsigned int i : regsToFlush)
  {
    ASSERT_MSG(DYNA_REC, !m_regs[i].IsLocked(), "Someone forgot to unlock PPC reg %u (X64 reg %i).",
               i, XFor(i));

    switch (m_regs[i].IsAway())
    {
    case PPCCachedReg::AwayLocation::NotAway:
      // We can have a cached value without a host register through speculative constants.
      // It must be cleared when flushing, otherwise it may be out of sync with PPCSTATE,
      // if PPCSTATE is modified externally (e.g. fallback to interpreter).
      if (m_regs[i].IsSpeculativeImm())
      {
        m_regs[i].Flushed();
      }
      break;
    case PPCCachedReg::AwayLocation::Bound:
    case PPCCachedReg::AwayLocation::Immediate:
      StoreFromRegister(i, mode);
      break;
    }
  }
}

void RegCache::FlushLockX(X64Reg reg)
{
  FlushX(reg);
  LockX(reg);
}

void RegCache::FlushLockX(X64Reg reg1, X64Reg reg2)
{
  FlushX(reg1);
  FlushX(reg2);
  LockX(reg1);
  LockX(reg2);
}

int RegCache::SanityCheck() const
{
  for (size_t i = 0; i < m_regs.size(); i++)
  {
    switch (m_regs[i].IsAway())
    {
    case PPCCachedReg::AwayLocation::NotAway:
      break;
    case PPCCachedReg::AwayLocation::Bound:
    {
      Gen::X64Reg simple = m_regs[i].Location().GetSimpleReg();
      if (m_xregs[simple].IsLocked())
        return 1;
      if (m_xregs[simple].Contents() != i)
        return 2;
      break;
    }
    case PPCCachedReg::AwayLocation::Immediate:
      return 3;
    }
  }
  return 0;
}

void RegCache::KillImmediate(size_t preg, bool doLoad, bool makeDirty)
{
  switch (m_regs[preg].IsAway())
  {
  case PPCCachedReg::AwayLocation::NotAway:
    break;
  case PPCCachedReg::AwayLocation::Bound:
    m_xregs[XFor(preg)].MakeDirty(makeDirty);
    break;
  case PPCCachedReg::AwayLocation::Immediate:
    BindToRegister(preg, doLoad, makeDirty);
    break;
  }
}

void RegCache::BindToRegister(size_t i, bool doLoad, bool makeDirty, RegRep finalRep, RegRep loadRep)
{
  if (!m_regs[i].IsBound())
  {
    X64Reg xr = GetFreeXReg();

    ASSERT_MSG(DYNA_REC, !m_xregs[xr].IsDirty(), "Xreg %i already dirty", xr);
    ASSERT_MSG(DYNA_REC, !m_xregs[xr].IsLocked(), "GetFreeXReg returned locked register");

    bool isAway = m_regs[i].IsAway() != PPCCachedReg::AwayLocation::NotAway;
    m_xregs[xr].BoundTo(i, makeDirty || isAway);

    if (doLoad)
    {
      LoadRegister(i, xr, loadRep);
    }

    ASSERT_MSG(DYNA_REC,
               !std::any_of(m_regs.begin(), m_regs.end(),
                            [xr](const auto& r) { return r.Location().IsSimpleReg(xr); }),
               "Xreg %i already bound", xr);

    m_regs[i].BoundTo(xr, finalRep);
  }
  else
  {
    // reg location must be simplereg; memory locations
    // and immediates are taken care of above.
    m_xregs[XFor(i)].MakeDirty(makeDirty);
    if (doLoad && m_regs[i].Rep() != loadRep)
    {
      Convert(XFor(i), m_regs[i].Rep(), loadRep);
    }
    m_regs[i].Converted(finalRep);
  }

  ASSERT_MSG(DYNA_REC, !m_xregs[XFor(i)].IsLocked(), "WTF, this reg should have been flushed");
}

void RegCache::StoreFromRegister(size_t i, FlushMode mode)
{
  bool doStore = false;

  switch (m_regs[i].IsAway())
  {
  case PPCCachedReg::AwayLocation::NotAway:
    return;
  case PPCCachedReg::AwayLocation::Bound:
  {
    X64Reg xr = XFor(i);
    doStore = m_xregs[xr].IsDirty();
    if (mode == FlushMode::All)
      m_xregs[xr].Flushed();
    break;
  }
  case PPCCachedReg::AwayLocation::Immediate:
    doStore = true;
    break;
  }

  if (doStore)
    StoreRegister(i, GetDefaultLocation(i), m_regs[i].Rep());
  if (mode == FlushMode::All)
    m_regs[i].Flushed();
}

OpArg RegCache::R(size_t preg, RegRep rep)
{
  if (m_regs[preg].IsBound())
    return ::Gen::R(RX(preg, rep));

  if (rep != m_regs[preg].Rep())
  {
    BindToRegister(preg, true, false, rep);
  }
  return m_regs[preg].Location();
}

X64Reg RegCache::RX(size_t preg, RegRep rep)
{
  ASSERT_MSG(DYNA_REC, m_regs[preg].IsBound(), "Unbound register - %zu", preg);

  if (rep != m_regs[preg].Rep())
  {
    Convert(XFor(preg), m_regs[preg].Rep(), rep);
    m_regs[preg].Converted(rep);
  }
  return XFor(preg);
}

void RegCache::UnlockAll()
{
  for (auto& reg : m_regs)
    reg.Unlock();
}

void RegCache::UnlockAllX()
{
  for (auto& xreg : m_xregs)
    xreg.Unlock();
}

bool RegCache::IsFreeX(size_t xreg) const
{
  return m_xregs[xreg].IsFree();
}

X64Reg RegCache::GetFreeXReg()
{
  size_t aCount;
  const X64Reg* aOrder = GetAllocationOrder(&aCount);
  for (size_t i = 0; i < aCount; i++)
  {
    X64Reg xr = aOrder[i];
    if (m_xregs[xr].IsFree())
    {
      return xr;
    }
  }

  // Okay, not found; run the register allocator heuristic and figure out which register we should
  // clobber.
  float min_score = std::numeric_limits<float>::max();
  X64Reg best_xreg = INVALID_REG;
  size_t best_preg = 0;
  for (size_t i = 0; i < aCount; i++)
  {
    X64Reg xreg = (X64Reg)aOrder[i];
    size_t preg = m_xregs[xreg].Contents();
    if (m_xregs[xreg].IsLocked() || m_regs[preg].IsLocked())
      continue;
    float score = ScoreRegister(xreg);
    if (score < min_score)
    {
      min_score = score;
      best_xreg = xreg;
      best_preg = preg;
    }
  }

  if (best_xreg != INVALID_REG)
  {
    StoreFromRegister(best_preg);
    return best_xreg;
  }

  // Still no dice? Die!
  ASSERT_MSG(DYNA_REC, false, "Regcache ran out of regs");
  return INVALID_REG;
}

int RegCache::NumFreeRegisters() const
{
  int count = 0;
  size_t aCount;
  const X64Reg* aOrder = GetAllocationOrder(&aCount);
  for (size_t i = 0; i < aCount; i++)
    if (m_xregs[aOrder[i]].IsFree())
      count++;
  return count;
}

void RegCache::FlushX(X64Reg reg)
{
  ASSERT_MSG(DYNA_REC, reg < m_xregs.size(), "Flushing non-existent reg %i", reg);
  ASSERT(!m_xregs[reg].IsLocked());
  if (!m_xregs[reg].IsFree())
  {
    StoreFromRegister(m_xregs[reg].Contents());
  }
}

X64Reg RegCache::XFor(size_t preg) const
{
  ASSERT_MSG(DYNA_REC, m_regs[preg].IsBound(), "Unbound register - %zu", preg);
  return m_regs[preg].Location().GetSimpleReg();
}

// Estimate roughly how bad it would be to de-allocate this register. Higher score
// means more bad.
float RegCache::ScoreRegister(X64Reg xreg) const
{
  size_t preg = m_xregs[xreg].Contents();
  float score = 0;

  // If it's not dirty, we don't need a store to write it back to the register file, so
  // bias a bit against dirty registers. Testing shows that a bias of 2 seems roughly
  // right: 3 causes too many extra clobbers, while 1 saves very few clobbers relative
  // to the number of extra stores it causes.
  if (m_xregs[xreg].IsDirty())
    score += 2;

  // If the register isn't actually needed in a physical register for a later instruction,
  // writing it back to the register file isn't quite as bad.
  if (GetRegUtilization()[preg])
  {
    // Don't look too far ahead; we don't want to have quadratic compilation times for
    // enormous block sizes!
    // This actually improves register allocation a tiny bit; I'm not sure why.
    u32 lookahead = std::min(m_jit.js.instructionsLeft, 64);
    // Count how many other registers are going to be used before we need this one again.
    u32 regs_in_count = CountRegsIn(preg, lookahead).Count();
    // Totally ad-hoc heuristic to bias based on how many other registers we'll need
    // before this one gets used again.
    score += 1 + 2 * (5 - log2f(1 + (float)regs_in_count));
  }

  return score;
}
