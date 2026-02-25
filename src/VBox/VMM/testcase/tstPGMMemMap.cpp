/* $Id$ */
/** @file
 * PGM memory mapping unit tests.
 *
 * Tests RAM range registration, enumeration, boundary lookups,
 * and physical memory read/write operations.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM_PHYS
#define VBOX_WITHOUT_PAGING_BIT_FIELDS
#include <VBox/vmm/pgm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"

#include <iprt/assert.h>
#include <iprt/string.h>

#include "tstVMMUnitTests-1.h"


/*********************************************************************************************************************************
*   Internal Functions (EMT callbacks)                                                                                           *
*********************************************************************************************************************************/

/** Callback for registering a 64K RAM range at a high address, executed on EMT. */
static DECLCALLBACK(int) testRegisterHighRam(PVM pVM)
{
    return PGMR3PhysRegisterRam(pVM, _4G + _1M, _64K, "TestHighRAM");
}


/** Callback for registering a small 4K RAM range, executed on EMT. */
static DECLCALLBACK(int) testRegisterSmallRam(PVM pVM)
{
    return PGMR3PhysRegisterRam(pVM, _4G + _4M, GUEST_PAGE_SIZE, "TestSmallRAM_4K");
}


/*********************************************************************************************************************************
*   Test: RAM Range Enumeration                                                                                                  *
*********************************************************************************************************************************/

/**
 * Tests PGMR3PhysGetRamRangeCount and PGMR3PhysGetRange to verify that
 * the VM's initial RAM ranges can be enumerated and have sensible values.
 */
static void testRamRangeEnumeration(PVM pVM)
{
    RTTestISub("RAM range enumeration");

    uint32_t cRanges = PGMR3PhysGetRamRangeCount(pVM);
    RTTESTI_CHECK_MSG(cRanges > 0, ("Expected at least one RAM range, got %u\n", cRanges));

    for (uint32_t i = 0; i < cRanges; i++)
    {
        RTGCPHYS    GCPhysStart = 0;
        RTGCPHYS    GCPhysLast  = 0;
        const char *pszDesc     = NULL;
        bool        fIsMmio     = false;

        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, &pszDesc, &fIsMmio);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK_MSG(GCPhysLast >= GCPhysStart,
                              ("Range %u: GCPhysStart=%RGp > GCPhysLast=%RGp\n", i, GCPhysStart, GCPhysLast));
            RTTESTI_CHECK_MSG(pszDesc != NULL && *pszDesc != '\0',
                              ("Range %u: description is NULL or empty\n", i));
        }
    }

    int rc = PGMR3PhysGetRange(pVM, cRanges, NULL, NULL, NULL, NULL);
    RTTESTI_CHECK_RC(rc, VERR_OUT_OF_RANGE);
}


/*********************************************************************************************************************************
*   Test: RAM Registration and Lookup                                                                                            *
*********************************************************************************************************************************/

/**
 * Tests that registering new RAM ranges at high addresses is reflected
 * in the range count and lookup table.
 */
static void testRamRegistration(PVM pVM)
{
    RTTestISub("RAM registration at high addresses");

    uint32_t cRangesBefore = PGMR3PhysGetRamRangeCount(pVM);

    RTTESTI_CHECK_RC_OK(VMR3ReqCallWait(pVM, 0, (PFNRT)testRegisterHighRam, 1, pVM));

    uint32_t cRangesAfter = PGMR3PhysGetRamRangeCount(pVM);
    RTTESTI_CHECK_MSG(cRangesAfter > cRangesBefore,
                      ("Range count did not increase: before=%u after=%u\n", cRangesBefore, cRangesAfter));

    bool fFoundNew = false;
    for (uint32_t i = 0; i < cRangesAfter; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        RTGCPHYS GCPhysLast  = 0;
        const char *pszDesc  = NULL;
        bool fIsMmio         = false;

        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, &pszDesc, &fIsMmio);
        if (RT_SUCCESS(rc) && GCPhysStart == _4G + _1M)
        {
            fFoundNew = true;
            RTTESTI_CHECK_MSG(GCPhysLast == _4G + _1M + _64K - 1,
                              ("Unexpected GCPhysLast=%RGp\n", GCPhysLast));
            RTTESTI_CHECK_MSG(!fIsMmio, ("New RAM range reported as MMIO\n"));
            break;
        }
    }
    RTTESTI_CHECK_MSG(fFoundNew, ("Newly registered RAM range at 4G+1M not found in enumeration\n"));
}


/*********************************************************************************************************************************
*   Test: Small RAM Range Registration                                                                                           *
*********************************************************************************************************************************/

/**
 * Tests registering a minimal (single page) RAM range.
 */
static void testSmallRamRegistration(PVM pVM)
{
    RTTestISub("Single-page RAM registration");

    uint32_t cRangesBefore = PGMR3PhysGetRamRangeCount(pVM);

    RTTESTI_CHECK_RC_OK(VMR3ReqCallWait(pVM, 0, (PFNRT)testRegisterSmallRam, 1, pVM));

    uint32_t cRangesAfter = PGMR3PhysGetRamRangeCount(pVM);
    RTTESTI_CHECK_MSG(cRangesAfter > cRangesBefore,
                      ("Range count did not increase for single-page RAM\n"));

    bool fFound = false;
    for (uint32_t i = 0; i < cRangesAfter; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        RTGCPHYS GCPhysLast  = 0;
        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, NULL, NULL);
        if (RT_SUCCESS(rc) && GCPhysStart == _4G + _4M)
        {
            fFound = true;
            RTTESTI_CHECK_MSG(GCPhysLast == _4G + _4M + GUEST_PAGE_SIZE - 1,
                              ("Single-page range has wrong GCPhysLast=%RGp expected=%RGp\n",
                               GCPhysLast, (RTGCPHYS)(_4G + _4M + GUEST_PAGE_SIZE - 1)));
            break;
        }
    }
    RTTESTI_CHECK_MSG(fFound, ("Single-page RAM range at 4G+4M not found\n"));
}


/*********************************************************************************************************************************
*   Test: Boundary Address Lookups                                                                                               *
*********************************************************************************************************************************/

/**
 * Tests that the RAM range lookup functions correctly handle boundary
 * addresses: start of first range, end of last range, addresses in gaps,
 * and NIL_RTGCPHYS.
 */
static void testBoundaryLookups(PVM pVM)
{
    RTTestISub("Boundary address lookups");

    uint32_t const cRanges = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    RTTESTI_CHECK_MSG(cRanges > 0, ("No lookup entries\n"));
    if (cRanges == 0)
        return;

    /* Test: First address of the first range. */
    RTGCPHYS const GCPhysFirstOfFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[0]);
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysFirstOfFirst);
        RTTESTI_CHECK_MSG(pRange != NULL, ("Lookup at first range start %RGp returned NULL\n", GCPhysFirstOfFirst));
        if (pRange)
            RTTESTI_CHECK(pRange->GCPhys == GCPhysFirstOfFirst);
    }

    /* Test: Last address of the last range. */
    RTGCPHYS const GCPhysLastOfLast = pVM->pgm.s.aRamRangeLookup[cRanges - 1].GCPhysLast;
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysLastOfLast);
        RTTESTI_CHECK_MSG(pRange != NULL, ("Lookup at last range end %RGp returned NULL\n", GCPhysLastOfLast));
    }

    /* Test: Address just past the last range should return NULL. */
    if (GCPhysLastOfLast < RTGCPHYS_MAX)
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysLastOfLast + 1);
        RTTESTI_CHECK_MSG(pRange == NULL, ("Lookup past last range returned non-NULL: GCPhys=%RGp\n", GCPhysLastOfLast + 1));
    }

    /* Test: NIL_RTGCPHYS should return NULL. */
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, NIL_RTGCPHYS);
        RTTESTI_CHECK_MSG(pRange == NULL, ("Lookup of NIL_RTGCPHYS returned non-NULL\n"));
    }

    /* Test: Address before the first range should return NULL (if first range doesn't start at 0). */
    if (GCPhysFirstOfFirst > 0)
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysFirstOfFirst - 1);
        RTTESTI_CHECK_MSG(pRange == NULL,
                          ("Lookup before first range start returned non-NULL: GCPhys=%RGp\n", GCPhysFirstOfFirst - 1));
    }
}


/*********************************************************************************************************************************
*   Test: Gap Detection Between Ranges                                                                                           *
*********************************************************************************************************************************/

/**
 * Tests that addresses falling in gaps between registered RAM ranges
 * are correctly reported as invalid by the lookup functions.
 */
static void testGapDetection(PVM pVM)
{
    RTTestISub("Gap detection between ranges");

    uint32_t const cRanges = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    uint32_t       cGapsFound = 0;

    for (uint32_t idx = 1; idx < cRanges; idx++)
    {
        RTGCPHYS const GCPhysPrevLast  = pVM->pgm.s.aRamRangeLookup[idx - 1].GCPhysLast;
        RTGCPHYS const GCPhysCurrFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);

        if (GCPhysCurrFirst > GCPhysPrevLast + 1)
        {
            cGapsFound++;

            /* Address just past previous range should NOT be found. */
            PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysPrevLast + 1);
            RTTESTI_CHECK_MSG(pRange == NULL,
                              ("Gap address %RGp (after range ending %RGp) found range %RGp\n",
                               GCPhysPrevLast + 1, GCPhysPrevLast, pRange ? pRange->GCPhys : 0));

            /* Address just before next range should NOT be found. */
            pRange = pgmPhysGetRangeSlow(pVM, GCPhysCurrFirst - 1);
            RTTESTI_CHECK_MSG(pRange == NULL,
                              ("Gap address %RGp (before range starting %RGp) found range %RGp\n",
                               GCPhysCurrFirst - 1, GCPhysCurrFirst, pRange ? pRange->GCPhys : 0));

            /* Middle of gap should NOT be found. */
            RTGCPHYS const GCPhysMid = GCPhysPrevLast + 1 + (GCPhysCurrFirst - GCPhysPrevLast - 1) / 2;
            pRange = pgmPhysGetRangeSlow(pVM, GCPhysMid);
            RTTESTI_CHECK_MSG(pRange == NULL,
                              ("Gap midpoint %RGp found range %RGp\n",
                               GCPhysMid, pRange ? pRange->GCPhys : 0));

            /* GetRangeAtOrAbove from gap should return the next range. */
            pRange = pgmPhysGetRangeAtOrAboveSlow(pVM, GCPhysPrevLast + 1);
            RTTESTI_CHECK_MSG(pRange != NULL && pRange->GCPhys == GCPhysCurrFirst,
                              ("GetRangeAtOrAbove from gap %RGp: expected range at %RGp, got %RGp\n",
                               GCPhysPrevLast + 1, GCPhysCurrFirst, pRange ? pRange->GCPhys : 0));
        }
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  Found and verified %u gap(s) between %u ranges\n", cGapsFound, cRanges);
}


/*********************************************************************************************************************************
*   Test: Physical Memory Read/Write                                                                                             *
*********************************************************************************************************************************/

/**
 * Tests basic physical memory read/write operations on registered RAM.
 * Writes a known pattern, reads it back, and verifies the data matches.
 */
static void testPhysReadWrite(PVM pVM)
{
    RTTestISub("Physical memory read/write");

    /* Find the first non-MMIO RAM range to test read/write. */
    uint32_t cRanges = PGMR3PhysGetRamRangeCount(pVM);
    RTGCPHYS GCPhysTest = NIL_RTGCPHYS;

    for (uint32_t i = 0; i < cRanges; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        RTGCPHYS GCPhysLast  = 0;
        bool     fIsMmio     = false;
        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, NULL, &fIsMmio);
        if (RT_SUCCESS(rc) && !fIsMmio && (GCPhysLast - GCPhysStart) >= _4K)
        {
            GCPhysTest = GCPhysStart;
            break;
        }
    }

    if (GCPhysTest == NIL_RTGCPHYS)
    {
        RTTestSkipped(g_hTest, "No suitable RAM range found for read/write test");
        return;
    }

    /* Write a pattern. */
    uint8_t abWrite[64];
    for (unsigned i = 0; i < sizeof(abWrite); i++)
        abWrite[i] = (uint8_t)(i ^ 0xA5);

    int rc = PGMR3PhysWriteExternal(pVM, GCPhysTest, abWrite, sizeof(abWrite), PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Read it back. */
    uint8_t abRead[64];
    RT_ZERO(abRead);
    rc = PGMR3PhysReadExternal(pVM, GCPhysTest, abRead, sizeof(abRead), PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Verify. */
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_MSG(memcmp(abWrite, abRead, sizeof(abWrite)) == 0,
                          ("Read-back data mismatch at GCPhys=%RGp\n", GCPhysTest));
    }

    /* Test typed read/write helpers. */
    PGMR3PhysWriteU32(pVM, GCPhysTest, UINT32_C(0xDEADBEEF), PGMACCESSORIGIN_DEBUGGER);
    uint32_t u32Val = PGMR3PhysReadU32(pVM, GCPhysTest, PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_MSG(u32Val == UINT32_C(0xDEADBEEF),
                      ("U32 read-back mismatch: got %#x expected %#x\n", u32Val, UINT32_C(0xDEADBEEF)));

    PGMR3PhysWriteU8(pVM, GCPhysTest + 16, 0x42, PGMACCESSORIGIN_DEBUGGER);
    uint8_t u8Val = PGMR3PhysReadU8(pVM, GCPhysTest + 16, PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_MSG(u8Val == 0x42, ("U8 read-back mismatch: got %#x expected %#x\n", u8Val, 0x42));

    PGMR3PhysWriteU64(pVM, GCPhysTest + 32, UINT64_C(0x123456789ABCDEF0), PGMACCESSORIGIN_DEBUGGER);
    uint64_t u64Val = PGMR3PhysReadU64(pVM, GCPhysTest + 32, PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_MSG(u64Val == UINT64_C(0x123456789ABCDEF0),
                      ("U64 read-back mismatch: got %#RX64 expected %#RX64\n", u64Val, UINT64_C(0x123456789ABCDEF0)));
}


/*********************************************************************************************************************************
*   Test: GCPhys2CCPtr Mapping                                                                                                   *
*********************************************************************************************************************************/

/**
 * Tests mapping guest physical addresses to host pointers via
 * PGMR3PhysGCPhys2CCPtrExternal and PGMR3PhysGCPhys2CCPtrReadOnlyExternal.
 */
static void testGCPhys2CCPtr(PVM pVM)
{
    RTTestISub("GCPhys2CCPtr mapping");

    /* Find a suitable RAM address. */
    uint32_t cRanges = PGMR3PhysGetRamRangeCount(pVM);
    RTGCPHYS GCPhysTest = NIL_RTGCPHYS;

    for (uint32_t i = 0; i < cRanges; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        RTGCPHYS GCPhysLast  = 0;
        bool     fIsMmio     = false;
        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, NULL, &fIsMmio);
        if (RT_SUCCESS(rc) && !fIsMmio && (GCPhysLast - GCPhysStart) >= _4K)
        {
            GCPhysTest = GCPhysStart;
            break;
        }
    }

    if (GCPhysTest == NIL_RTGCPHYS)
    {
        RTTestSkipped(g_hTest, "No suitable RAM range for GCPhys2CCPtr test");
        return;
    }

    /* Test writable mapping. */
    {
        void            *pv   = NULL;
        PGMPAGEMAPLOCK   Lock;
        int rc = PGMR3PhysGCPhys2CCPtrExternal(pVM, GCPhysTest, &pv, &Lock);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK_MSG(pv != NULL, ("GCPhys2CCPtr returned NULL pointer\n"));

            /* Write through the mapping. */
            if (pv)
                *(uint32_t *)pv = UINT32_C(0xCAFEBABE);

            PGMPhysReleasePageMappingLock(pVM, &Lock);

            /* Verify through the standard read API. */
            uint32_t u32 = PGMR3PhysReadU32(pVM, GCPhysTest, PGMACCESSORIGIN_DEBUGGER);
            RTTESTI_CHECK_MSG(u32 == UINT32_C(0xCAFEBABE),
                              ("Write via CCPtr not visible through ReadU32: got %#x\n", u32));
        }
    }

    /* Test read-only mapping. */
    {
        void const      *pv   = NULL;
        PGMPAGEMAPLOCK   Lock;
        int rc = PGMR3PhysGCPhys2CCPtrReadOnlyExternal(pVM, GCPhysTest, &pv, &Lock);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK_MSG(pv != NULL, ("GCPhys2CCPtrReadOnly returned NULL pointer\n"));
            if (pv)
            {
                uint32_t u32 = *(uint32_t const *)pv;
                RTTESTI_CHECK_MSG(u32 == UINT32_C(0xCAFEBABE),
                                  ("Read-only CCPtr data mismatch: got %#x expected CAFEBABE\n", u32));
            }
            PGMPhysReleasePageMappingLock(pVM, &Lock);
        }
    }
}


/*********************************************************************************************************************************
*   Test: Range Lookup with At-or-Above Semantics                                                                                *
*********************************************************************************************************************************/

/**
 * Tests pgmPhysGetRangeAtOrAboveSlow which returns the range at or above
 * a given address, useful for iterating through the address space.
 */
static void testRangeAtOrAbove(PVM pVM)
{
    RTTestISub("Range at-or-above lookups");

    uint32_t const cRanges = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    if (cRanges == 0)
    {
        RTTestSkipped(g_hTest, "No lookup entries");
        return;
    }

    /* Lookup from address 0 should find the first range. */
    RTGCPHYS const GCPhysFirstRange = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[0]);
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeAtOrAboveSlow(pVM, 0);
        RTTESTI_CHECK_MSG(pRange != NULL, ("AtOrAbove(0) returned NULL\n"));
        if (pRange)
            RTTESTI_CHECK_MSG(pRange->GCPhys == GCPhysFirstRange,
                              ("AtOrAbove(0) returned %RGp, expected %RGp\n", pRange->GCPhys, GCPhysFirstRange));
    }

    /* Lookup from the exact start of each range should return that range. */
    for (uint32_t idx = 0; idx < cRanges; idx++)
    {
        RTGCPHYS const GCPhysFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);
        PPGMRAMRANGE pRange = pgmPhysGetRangeAtOrAboveSlow(pVM, GCPhysFirst);
        RTTESTI_CHECK_MSG(pRange != NULL && pRange->GCPhys == GCPhysFirst,
                          ("AtOrAbove(%RGp) returned %RGp, expected %RGp\n",
                           GCPhysFirst, pRange ? pRange->GCPhys : 0, GCPhysFirst));
    }

    /* Lookup past the last range should return NULL. */
    RTGCPHYS const GCPhysLastOfLast = pVM->pgm.s.aRamRangeLookup[cRanges - 1].GCPhysLast;
    if (GCPhysLastOfLast < RTGCPHYS_MAX)
    {
        PPGMRAMRANGE pRange = pgmPhysGetRangeAtOrAboveSlow(pVM, GCPhysLastOfLast + 1);
        RTTESTI_CHECK_MSG(pRange == NULL, ("AtOrAbove past last range returned non-NULL\n"));
    }
}


/*********************************************************************************************************************************
*   Test: Integrity Check                                                                                                        *
*********************************************************************************************************************************/

/**
 * Runs the built-in PGM integrity checker to verify internal
 * consistency of the page management data structures after our
 * test modifications.
 */
static void testPGMIntegrity(PVM pVM)
{
    RTTestISub("PGM integrity check");
    int rc = PGMR3CheckIntegrity(pVM);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}


/*********************************************************************************************************************************
*   Public Entry Point                                                                                                           *
*********************************************************************************************************************************/

void testPGMMemMap(PVM pVM)
{
    /* US1: RAM range enumeration and lookup. */
    testRamRangeEnumeration(pVM);
    testRamRegistration(pVM);
    testSmallRamRegistration(pVM);
    testBoundaryLookups(pVM);
    testGapDetection(pVM);
    testRangeAtOrAbove(pVM);

    /* US2: Physical memory read/write and mapping. */
    testPhysReadWrite(pVM);
    testGCPhys2CCPtr(pVM);

    /* Final integrity check. */
    testPGMIntegrity(pVM);
}
