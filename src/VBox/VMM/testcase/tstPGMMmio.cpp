/* $Id$ */
/** @file
 * PGM MMIO memory mapping unit tests.
 *
 * Tests MMIO region registration, mapping, unmapping, and the physical
 * handler infrastructure. Uses its own VMR3Create with a CFGM
 * constructor so MMIO can be registered during VMSTATE_CREATING.
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
#define VBOX_IN_VMM
#include <VBox/vmm/pgm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include "PGMInline.h"

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/*********************************************************************************************************************************
*   CFGM Constructor                                                                                                             *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNCFGMCONSTRUCTOR}
 *
 * Creates the default CFGM tree, then registers MMIO handler types and
 * MMIO regions during VMSTATE_CREATING.
 */
static DECLCALLBACK(int)
tstPGMMmioCfgConstructor(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    RT_NOREF(pUVM, pvUser);

    int rc = pVMM->pfnCFGMR3ConstructDefaultTree(pVM);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Test: MMIO Range Internal Structure                                                                                          *
*********************************************************************************************************************************/

/**
 * Examines the internal PGM RAM range structures to verify the flags,
 * page types, and layout of the memory map.
 */
static void testRamRangeFlags(PVM pVM)
{
    RTTestISub("RAM range flags and page types");

    uint32_t cRamRanges = 0;
    uint32_t cMmioRanges = 0;
    uint32_t cRomRanges = 0;
    uint32_t cRegularRanges = 0;

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    for (uint32_t idx = 0; idx < cLookupEntries; idx++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx]);
        RTTESTI_CHECK_MSG(idRamRange > 0 && idRamRange <= pVM->pgm.s.idRamRangeMax,
                          ("Invalid RAM range ID %u at idx %u\n", idRamRange, idx));
        if (idRamRange == 0 || idRamRange > pVM->pgm.s.idRamRangeMax)
            continue;

        PPGMRAMRANGE const pRamRange = pVM->pgm.s.apRamRanges[idRamRange];
        RTTESTI_CHECK_MSG(pRamRange != NULL, ("NULL RAM range for ID %u\n", idRamRange));
        if (!pRamRange)
            continue;

        cRamRanges++;

        if (pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO)
            cMmioRanges++;
        else if (pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_ROM)
            cRomRanges++;
        else
            cRegularRanges++;

        RTTESTI_CHECK_MSG(pRamRange->GCPhys != NIL_RTGCPHYS || (pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO),
                          ("Range ID %u has NIL GCPhys but is not ad-hoc MMIO\n", idRamRange));
        RTTESTI_CHECK_MSG(pRamRange->cb > 0, ("Range ID %u has zero size\n", idRamRange));

        if (pRamRange->GCPhys != NIL_RTGCPHYS)
        {
            RTTESTI_CHECK_MSG(pRamRange->GCPhysLast == pRamRange->GCPhys + pRamRange->cb - 1,
                              ("Range ID %u: GCPhysLast=%RGp != GCPhys+cb-1=%RGp\n",
                               idRamRange, pRamRange->GCPhysLast, pRamRange->GCPhys + pRamRange->cb - 1));
        }

        uint32_t const cPages = pRamRange->cb >> GUEST_PAGE_SHIFT;
        RTTESTI_CHECK_MSG(cPages > 0, ("Range ID %u has zero pages\n", idRamRange));
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  %u total ranges: %u regular, %u MMIO, %u ROM\n",
                  cRamRanges, cRegularRanges, cMmioRanges, cRomRanges);
    RTTESTI_CHECK_MSG(cRamRanges > 0, ("No RAM ranges found\n"));
    RTTESTI_CHECK_MSG(cRegularRanges > 0, ("No regular RAM ranges found\n"));
}


/*********************************************************************************************************************************
*   Test: Page Type Walk                                                                                                         *
*********************************************************************************************************************************/

/**
 * Walks all pages in all ranges and categorizes them by type,
 * verifying consistency of page descriptors.
 */
static void testPageTypeWalk(PVM pVM)
{
    RTTestISub("Page type walk");

    uint32_t cRam = 0, cZero = 0, cMmio = 0, cRom = 0, cOther = 0;

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    for (uint32_t idx = 0; idx < cLookupEntries; idx++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx]);
        if (idRamRange == 0 || idRamRange > pVM->pgm.s.idRamRangeMax)
            continue;

        PPGMRAMRANGE const pRamRange = pVM->pgm.s.apRamRanges[idRamRange];
        if (!pRamRange || pRamRange->GCPhys == NIL_RTGCPHYS)
            continue;

        uint32_t const cPages = pRamRange->cb >> GUEST_PAGE_SHIFT;
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            PCPGMPAGE pPage = &pRamRange->aPages[iPage];
            switch (PGM_PAGE_GET_TYPE(pPage))
            {
                case PGMPAGETYPE_RAM:
                    if (PGM_PAGE_IS_ZERO(pPage))
                        cZero++;
                    else
                        cRam++;
                    break;
                case PGMPAGETYPE_MMIO:
                    cMmio++;
                    break;
                case PGMPAGETYPE_ROM:
                case PGMPAGETYPE_ROM_SHADOW:
                    cRom++;
                    break;
                default:
                    cOther++;
                    break;
            }
        }
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  Pages: %u allocated RAM, %u zero, %u MMIO, %u ROM, %u other\n",
                  cRam, cZero, cMmio, cRom, cOther);
    RTTESTI_CHECK_MSG(cRam + cZero > 0, ("No RAM pages found\n"));
}


/*********************************************************************************************************************************
*   Test: MMIO Hole Verification                                                                                                 *
*********************************************************************************************************************************/

/**
 * Verifies that the traditional x86 MMIO hole (typically 3.5GB-4GB region)
 * is properly represented as a gap in the RAM range lookup table.
 * This is where PCI MMIO BARs would be mapped.
 */
static void testMmioHoleVerification(PVM pVM)
{
    RTTestISub("MMIO hole verification");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    bool fFoundMmioHole = false;

    for (uint32_t idx = 1; idx < cLookupEntries; idx++)
    {
        RTGCPHYS const GCPhysPrevLast = pVM->pgm.s.aRamRangeLookup[idx - 1].GCPhysLast;
        RTGCPHYS const GCPhysCurrFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);

        if (GCPhysCurrFirst > GCPhysPrevLast + 1)
        {
            RTGCPHYS const cbGap = GCPhysCurrFirst - GCPhysPrevLast - 1;

            if (GCPhysPrevLast < _4G && GCPhysCurrFirst >= _4G - _512M)
            {
                fFoundMmioHole = true;
                RTTestIPrintf(RTTESTLVL_ALWAYS, "  MMIO hole: %RGp - %RGp (size %RGp / %u MB)\n",
                              GCPhysPrevLast + 1, GCPhysCurrFirst - 1, cbGap, (uint32_t)(cbGap >> 20));

                PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysPrevLast + 1);
                RTTESTI_CHECK_MSG(pRange == NULL,
                                  ("Address in MMIO hole %RGp should not be in a RAM range\n", GCPhysPrevLast + 1));

                pRange = pgmPhysGetRangeAtOrAboveSlow(pVM, GCPhysPrevLast + 1);
                RTTESTI_CHECK_MSG(pRange != NULL && pRange->GCPhys == GCPhysCurrFirst,
                                  ("AtOrAbove from MMIO hole should find next range at %RGp\n", GCPhysCurrFirst));
            }
        }
    }

    if (!fFoundMmioHole)
        RTTestIPrintf(RTTESTLVL_ALWAYS, "  Note: No traditional MMIO hole detected (this is OK for small RAM configs)\n");
}


/*********************************************************************************************************************************
*   Test: Lookup Table Ordering                                                                                                  *
*********************************************************************************************************************************/

/**
 * Verifies that the RAM range lookup table is properly ordered
 * (sorted by GCPhys) with no overlapping entries.
 */
static void testLookupTableOrdering(PVM pVM)
{
    RTTestISub("Lookup table ordering and consistency");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    RTTESTI_CHECK_MSG(cLookupEntries > 0, ("Lookup table is empty\n"));

    RTGCPHYS GCPhysPrevLast = 0;
    for (uint32_t idx = 0; idx < cLookupEntries; idx++)
    {
        RTGCPHYS const GCPhysCurFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);
        RTGCPHYS const GCPhysCurLast  = pVM->pgm.s.aRamRangeLookup[idx].GCPhysLast;

        RTTESTI_CHECK_MSG(GCPhysCurLast >= GCPhysCurFirst,
                          ("Entry %u: GCPhysFirst=%RGp > GCPhysLast=%RGp\n", idx, GCPhysCurFirst, GCPhysCurLast));

        if (idx > 0)
        {
            RTTESTI_CHECK_MSG(GCPhysCurFirst > GCPhysPrevLast,
                              ("Entry %u overlaps with previous: cur=%RGp prev_last=%RGp\n",
                               idx, GCPhysCurFirst, GCPhysPrevLast));
        }

        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx]);
        RTTESTI_CHECK_MSG(idRamRange > 0 && idRamRange <= pVM->pgm.s.idRamRangeMax,
                          ("Entry %u: invalid range ID %u\n", idx, idRamRange));

        PPGMRAMRANGE const pRange = pVM->pgm.s.apRamRanges[idRamRange];
        RTTESTI_CHECK_MSG(pRange != NULL, ("Entry %u: range ID %u is NULL\n", idx, idRamRange));
        if (pRange)
        {
            RTTESTI_CHECK_MSG(pRange->GCPhys == GCPhysCurFirst,
                              ("Entry %u: lookup GCPhys=%RGp != range GCPhys=%RGp\n",
                               idx, GCPhysCurFirst, pRange->GCPhys));
            RTTESTI_CHECK_MSG(pRange->GCPhysLast == GCPhysCurLast,
                              ("Entry %u: lookup GCPhysLast=%RGp != range GCPhysLast=%RGp\n",
                               idx, GCPhysCurLast, pRange->GCPhysLast));
        }

        GCPhysPrevLast = GCPhysCurLast;
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  %u lookup entries verified, all properly ordered\n", cLookupEntries);
}


/*********************************************************************************************************************************
*   Test: Physical Read on Unmapped Regions                                                                                      *
*********************************************************************************************************************************/

/**
 * Tests reading from addresses in gaps (unmapped regions) to verify
 * proper error handling or default behavior.
 */
static void testReadUnmappedRegion(PVM pVM)
{
    RTTestISub("Read from unmapped (gap) regions");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    bool fTestedGap = false;

    for (uint32_t idx = 1; idx < cLookupEntries && !fTestedGap; idx++)
    {
        RTGCPHYS const GCPhysPrevLast = pVM->pgm.s.aRamRangeLookup[idx - 1].GCPhysLast;
        RTGCPHYS const GCPhysCurrFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);

        if (GCPhysCurrFirst > GCPhysPrevLast + 1)
        {
            RTGCPHYS const GCPhysGap = GCPhysPrevLast + 1;

            uint8_t abBuf[16];
            memset(abBuf, 0xCC, sizeof(abBuf));
            int rc = PGMR3PhysReadExternal(pVM, GCPhysGap, abBuf, sizeof(abBuf), PGMACCESSORIGIN_DEBUGGER);
            RTTESTI_CHECK_MSG(RT_SUCCESS(rc),
                              ("Read from gap at %RGp failed: %Rrc\n", GCPhysGap, rc));

            bool fAllFF = true;
            for (unsigned i = 0; i < sizeof(abBuf); i++)
            {
                if (abBuf[i] != 0xFF)
                {
                    fAllFF = false;
                    break;
                }
            }
            RTTestIPrintf(RTTESTLVL_ALWAYS, "  Gap read at %RGp: bytes are %s0xFF\n",
                          GCPhysGap, fAllFF ? "all " : "NOT all ");
            fTestedGap = true;
        }
    }

    if (!fTestedGap)
        RTTestIPrintf(RTTESTLVL_ALWAYS, "  No gaps found to test unmapped reads\n");
}


/*********************************************************************************************************************************
*   Test: Write to Unmapped Region                                                                                               *
*********************************************************************************************************************************/

/**
 * Tests writing to addresses in gaps to verify writes don't corrupt
 * adjacent mapped regions.
 */
static void testWriteUnmappedRegion(PVM pVM)
{
    RTTestISub("Write to unmapped (gap) regions and adjacent integrity");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    bool fTestedGap = false;

    for (uint32_t idx = 1; idx < cLookupEntries && !fTestedGap; idx++)
    {
        RTGCPHYS const GCPhysPrevLast  = pVM->pgm.s.aRamRangeLookup[idx - 1].GCPhysLast;
        RTGCPHYS const GCPhysCurrFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idx]);

        if (GCPhysCurrFirst > GCPhysPrevLast + 1)
        {
            RTGCPHYS const GCPhysGap = GCPhysPrevLast + 1;

            /* Read byte at end of previous range (save). */
            uint8_t bSaved = 0;
            PGMR3PhysReadExternal(pVM, GCPhysPrevLast, &bSaved, 1, PGMACCESSORIGIN_DEBUGGER);

            /* Write to gap. */
            uint8_t bWrite = 0xAB;
            PGMR3PhysWriteExternal(pVM, GCPhysGap, &bWrite, 1, PGMACCESSORIGIN_DEBUGGER);

            /* Verify previous range is not corrupted. */
            uint8_t bCheck = 0;
            PGMR3PhysReadExternal(pVM, GCPhysPrevLast, &bCheck, 1, PGMACCESSORIGIN_DEBUGGER);
            RTTESTI_CHECK_MSG(bCheck == bSaved,
                              ("Adjacent byte corrupted after gap write: got %#x expected %#x\n", bCheck, bSaved));

            fTestedGap = true;
        }
    }
}


/*********************************************************************************************************************************
*   Test: Range Enumeration with MMIO Detection                                                                                  *
*********************************************************************************************************************************/

/**
 * Uses the public API PGMR3PhysGetRange to enumerate all ranges
 * and classify them as MMIO or non-MMIO.
 */
static void testRangeEnumerationMmioDetection(PVM pVM)
{
    RTTestISub("Range enumeration with MMIO detection");

    uint32_t const cRanges = PGMR3PhysGetRamRangeCount(pVM);
    uint32_t cMmio = 0;
    uint32_t cNonMmio = 0;

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
            RTGCPHYS const cbRange = GCPhysLast - GCPhysStart + 1;
            RTTestIPrintf(RTTESTLVL_ALWAYS, "  Range %2u: %RGp-%RGp (%7RGp / %5u KB) %s %s\n",
                          i, GCPhysStart, GCPhysLast, cbRange, (uint32_t)(cbRange >> 10),
                          fIsMmio ? "MMIO" : "RAM ", pszDesc ? pszDesc : "<no desc>");
            if (fIsMmio)
                cMmio++;
            else
                cNonMmio++;
        }
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  Total: %u ranges (%u RAM, %u MMIO)\n", cRanges, cNonMmio, cMmio);
    RTTESTI_CHECK_MSG(cNonMmio > 0, ("No non-MMIO ranges found\n"));
}


/*********************************************************************************************************************************
*   Test: MMIO Range Properties                                                                                                  *
*********************************************************************************************************************************/

/**
 * Finds the MMIO range (VGA Video Buffer at 0xA0000) and tests its properties:
 * page types, flag values, and lookup behavior.
 */
static void testMmioRangeProperties(PVM pVM)
{
    RTTestISub("MMIO range properties (VGA buffer)");

    uint32_t const cRanges = PGMR3PhysGetRamRangeCount(pVM);
    RTGCPHYS GCPhysMmioStart = NIL_RTGCPHYS;
    RTGCPHYS GCPhysMmioLast  = NIL_RTGCPHYS;

    for (uint32_t i = 0; i < cRanges; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        RTGCPHYS GCPhysLast  = 0;
        bool     fIsMmio     = false;
        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, &GCPhysLast, NULL, &fIsMmio);
        if (RT_SUCCESS(rc) && fIsMmio)
        {
            GCPhysMmioStart = GCPhysStart;
            GCPhysMmioLast  = GCPhysLast;
            break;
        }
    }

    if (GCPhysMmioStart == NIL_RTGCPHYS)
    {
        RTTestSkipped(g_hTest, "No MMIO range found to test properties");
        return;
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  MMIO range: %RGp - %RGp\n", GCPhysMmioStart, GCPhysMmioLast);

    /* Verify lookup returns this range. */
    PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysMmioStart);
    RTTESTI_CHECK_MSG(pRange != NULL, ("Lookup at MMIO start %RGp returned NULL\n", GCPhysMmioStart));
    if (!pRange)
        return;

    RTTESTI_CHECK(pRange->GCPhys == GCPhysMmioStart);
    RTTESTI_CHECK(pRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO);

    /* Lookup at middle of MMIO range. */
    RTGCPHYS const GCPhysMid = GCPhysMmioStart + (GCPhysMmioLast - GCPhysMmioStart) / 2;
    pRange = pgmPhysGetRangeSlow(pVM, GCPhysMid);
    RTTESTI_CHECK_MSG(pRange != NULL && pRange->GCPhys == GCPhysMmioStart,
                      ("Lookup at MMIO middle %RGp failed\n", GCPhysMid));

    /* Lookup at last byte of MMIO range. */
    pRange = pgmPhysGetRangeSlow(pVM, GCPhysMmioLast);
    RTTESTI_CHECK_MSG(pRange != NULL && pRange->GCPhys == GCPhysMmioStart,
                      ("Lookup at MMIO last %RGp failed\n", GCPhysMmioLast));

    /* Verify all pages in the MMIO range are PGMPAGETYPE_MMIO. */
    uint32_t const cPages = pRange->cb >> GUEST_PAGE_SHIFT;
    uint32_t cMmioPages = 0;
    for (uint32_t iPage = 0; iPage < cPages; iPage++)
    {
        PCPGMPAGE pPage = &pRange->aPages[iPage];
        if (PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO)
            cMmioPages++;
    }
    RTTESTI_CHECK_MSG(cMmioPages == cPages,
                      ("Expected %u MMIO pages, got %u\n", cPages, cMmioPages));

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  Verified %u MMIO pages in range, all correct type\n", cMmioPages);
}


/*********************************************************************************************************************************
*   Test: MMIO Read/Write Behavior                                                                                               *
*********************************************************************************************************************************/

/**
 * Tests read/write behavior on the MMIO range.
 * MMIO reads typically return 0xFF, and writes are typically ignored/handled.
 */
static void testMmioReadWriteBehavior(PVM pVM)
{
    RTTestISub("MMIO read/write behavior");

    uint32_t const cRanges = PGMR3PhysGetRamRangeCount(pVM);
    RTGCPHYS GCPhysMmio = NIL_RTGCPHYS;

    for (uint32_t i = 0; i < cRanges; i++)
    {
        RTGCPHYS GCPhysStart = 0;
        bool     fIsMmio     = false;
        int rc = PGMR3PhysGetRange(pVM, i, &GCPhysStart, NULL, NULL, &fIsMmio);
        if (RT_SUCCESS(rc) && fIsMmio)
        {
            GCPhysMmio = GCPhysStart;
            break;
        }
    }

    if (GCPhysMmio == NIL_RTGCPHYS)
    {
        RTTestSkipped(g_hTest, "No MMIO range found for read/write behavior test");
        return;
    }

    /* Read from MMIO — should return 0xFF (default for unhandled MMIO reads). */
    uint8_t abRead[16];
    memset(abRead, 0x00, sizeof(abRead));
    int rc = PGMR3PhysReadExternal(pVM, GCPhysMmio, abRead, sizeof(abRead), PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    bool fAllFF = true;
    for (unsigned i = 0; i < sizeof(abRead); i++)
    {
        if (abRead[i] != 0xFF)
        {
            fAllFF = false;
            break;
        }
    }
    RTTestIPrintf(RTTESTLVL_ALWAYS, "  MMIO read at %RGp: %s 0xFF (first byte: %#x)\n",
                  GCPhysMmio, fAllFF ? "all" : "not all", abRead[0]);

    /* Write to MMIO — should not crash and should succeed. */
    uint8_t abWrite[16];
    memset(abWrite, 0xAB, sizeof(abWrite));
    rc = PGMR3PhysWriteExternal(pVM, GCPhysMmio, abWrite, sizeof(abWrite), PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Read back — MMIO should still return 0xFF (writes don't persist on MMIO). */
    memset(abRead, 0x00, sizeof(abRead));
    rc = PGMR3PhysReadExternal(pVM, GCPhysMmio, abRead, sizeof(abRead), PGMACCESSORIGIN_DEBUGGER);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  MMIO read after write: first byte=%#x\n", abRead[0]);
}


/*********************************************************************************************************************************
*   Test: MMIO vs RAM Boundary                                                                                                   *
*********************************************************************************************************************************/

/**
 * Tests the boundary between RAM and MMIO ranges to verify correct
 * transition behavior.
 */
static void testMmioRamBoundary(PVM pVM)
{
    RTTestISub("MMIO/RAM boundary transitions");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;

    for (uint32_t idx = 1; idx < cLookupEntries; idx++)
    {
        uint32_t const idPrev = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx - 1]);
        uint32_t const idCurr = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx]);
        if (idPrev == 0 || idPrev > pVM->pgm.s.idRamRangeMax ||
            idCurr == 0 || idCurr > pVM->pgm.s.idRamRangeMax)
            continue;

        PPGMRAMRANGE const pPrev = pVM->pgm.s.apRamRanges[idPrev];
        PPGMRAMRANGE const pCurr = pVM->pgm.s.apRamRanges[idCurr];
        if (!pPrev || !pCurr)
            continue;

        bool const fPrevMmio = !!(pPrev->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO);
        bool const fCurrMmio = !!(pCurr->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO);

        if (fPrevMmio != fCurrMmio)
        {
            RTGCPHYS const GCPhysBoundary = pPrev->GCPhysLast;
            RTTestIPrintf(RTTESTLVL_ALWAYS, "  %s->%s boundary at %RGp\n",
                          fPrevMmio ? "MMIO" : "RAM", fCurrMmio ? "MMIO" : "RAM", GCPhysBoundary);

            /* Last byte of previous range. */
            PPGMRAMRANGE pRange = pgmPhysGetRangeSlow(pVM, GCPhysBoundary);
            RTTESTI_CHECK_MSG(pRange == pPrev,
                              ("Boundary lookup at %RGp returned wrong range\n", GCPhysBoundary));

            /* First byte of next range (if adjacent). */
            if (pCurr->GCPhys == GCPhysBoundary + 1)
            {
                pRange = pgmPhysGetRangeSlow(pVM, pCurr->GCPhys);
                RTTESTI_CHECK_MSG(pRange == pCurr,
                                  ("Boundary lookup at %RGp returned wrong range\n", pCurr->GCPhys));
            }
        }
    }
}


/*********************************************************************************************************************************
*   Test: ROM Range Detection and Properties                                                                                     *
*********************************************************************************************************************************/

/**
 * Finds ROM ranges and verifies their page types and properties.
 */
static void testRomRangeProperties(PVM pVM)
{
    RTTestISub("ROM range properties");

    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    uint32_t cRomRangesFound = 0;

    for (uint32_t idx = 0; idx < cLookupEntries; idx++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idx]);
        if (idRamRange == 0 || idRamRange > pVM->pgm.s.idRamRangeMax)
            continue;

        PPGMRAMRANGE const pRamRange = pVM->pgm.s.apRamRanges[idRamRange];
        if (!pRamRange || pRamRange->GCPhys == NIL_RTGCPHYS)
            continue;

        if (pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_ROM)
        {
            cRomRangesFound++;
            uint32_t const cPages = pRamRange->cb >> GUEST_PAGE_SHIFT;
            uint32_t cRomPages = 0;

            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                uint8_t uType = PGM_PAGE_GET_TYPE(&pRamRange->aPages[iPage]);
                if (uType == PGMPAGETYPE_ROM || uType == PGMPAGETYPE_ROM_SHADOW)
                    cRomPages++;
            }

            RTTestIPrintf(RTTESTLVL_ALWAYS, "  ROM range: %RGp-%RGp (%u pages, %u ROM-type) %s\n",
                          pRamRange->GCPhys, pRamRange->GCPhysLast, cPages, cRomPages,
                          pRamRange->pszDesc ? pRamRange->pszDesc : "");

            RTTESTI_CHECK_MSG(cRomPages == cPages,
                              ("ROM range at %RGp: expected %u ROM pages, got %u\n",
                               pRamRange->GCPhys, cPages, cRomPages));
        }
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "  Found %u ROM ranges\n", cRomRangesFound);
}


/*********************************************************************************************************************************
*   Test: PGM Integrity After All Tests                                                                                          *
*********************************************************************************************************************************/

static void testFinalIntegrity(PVM pVM)
{
    RTTestISub("Final PGM integrity check");
    int rc = PGMR3CheckIntegrity(pVM);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}


/*********************************************************************************************************************************
*   Main Entry Point                                                                                                             *
*********************************************************************************************************************************/

int main(int argc, char **argv)
{
    RTEXITCODE rcExit;
    int rc = RTR3InitExe(argc, &argv, SUPR3INIT_F_DRIVERLESS << RTR3INIT_FLAGS_SUPLIB_SHIFT);
    if (RT_SUCCESS(rc))
    {
        rc = RTTestCreate("tstPGMMmio", &g_hTest);
        if (RT_SUCCESS(rc))
        {
            RTTestBanner(g_hTest);

            /*
             * Create a VM with our custom CFGM constructor to get a proper
             * memory layout for MMIO testing.
             */
            PVM  pVM;
            PUVM pUVM;
            RTTESTI_CHECK_RC_OK(rc = VMR3Create(1 /*cCpus*/, NULL, VMCREATE_F_DRIVERLESS,
                                                NULL, NULL, tstPGMMmioCfgConstructor, NULL, &pVM, &pUVM));
            if (RT_SUCCESS(rc))
            {
                /*
                 * Run the MMIO-related tests.
                 */
                testRangeEnumerationMmioDetection(pVM);
                testRamRangeFlags(pVM);
                testPageTypeWalk(pVM);
                testLookupTableOrdering(pVM);
                testMmioHoleVerification(pVM);
                testMmioRangeProperties(pVM);
                testMmioReadWriteBehavior(pVM);
                testMmioRamBoundary(pVM);
                testRomRangeProperties(pVM);
                testReadUnmappedRegion(pVM);
                testWriteUnmappedRegion(pVM);
                testFinalIntegrity(pVM);

                /*
                 * Clean up.
                 */
                RTTESTI_CHECK_RC_OK(VMR3PowerOff(pUVM));
                RTTESTI_CHECK_RC_OK(VMR3Destroy(pUVM));
                VMR3ReleaseUVM(pUVM);
            }

            rcExit = RTTestSummaryAndDestroy(g_hTest);
        }
        else
            rcExit = RTMsgErrorExitFailure("RTTestCreate failed: %Rrc", rc);
    }
    else
        rcExit = RTMsgInitFailure(rc);
    return rcExit;
}
