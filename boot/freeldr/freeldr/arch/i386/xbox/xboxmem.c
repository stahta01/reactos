/*
 *  FreeLoader
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  Note: much of this code was based on knowledge and/or code developed
 *  by the Xbox Linux group: http://www.xbox-linux.org
 */

#include <freeldr.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(MEMORY);

static ULONG InstalledMemoryMb = 0;
static ULONG AvailableMemoryMb = 0;
extern multiboot_info_t * MultibootInfoPtr;
extern PVOID FrameBuffer;
extern ULONG FrameBufferSize;

#define TEST_SIZE     0x200
#define TEST_PATTERN1 0xAA
#define TEST_PATTERN2 0x55

extern VOID
SetMemory(
    PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType);

extern VOID
ReserveMemory(
    PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType,
    PCHAR Usage);

extern ULONG
PcMemFinalizeMemoryMap(
    PFREELDR_MEMORY_DESCRIPTOR MemoryMap);

VOID
XboxMemInit(VOID)
{
    UCHAR ControlRegion[TEST_SIZE];
    PVOID MembaseTop = (PVOID)(64 * 1024 * 1024);
    PVOID MembaseLow = (PVOID)0;

    WRITE_REGISTER_ULONG((PULONG)NV2A_FB_CFG0, 0x03070103);
    WRITE_REGISTER_ULONG((PULONG)(NV2A_FB_CFG0 + 4), 0x11448000);

    /* Prep hardware for 128 Mb */
    WRITE_PORT_ULONG((PULONG)0xCF8, CONFIG_CMD(0, 0, 0x84));
    WRITE_PORT_ULONG((PULONG)0xCFC, 0x7FFFFFF);

    InstalledMemoryMb = 64;
    memset(ControlRegion, TEST_PATTERN1, TEST_SIZE);
    memset(MembaseTop, TEST_PATTERN1, TEST_SIZE);
    __wbinvd();

    if (memcmp(MembaseTop, ControlRegion, TEST_SIZE) == 0)
    {
        /* Looks like there is memory .. maybe a 128MB box */
        memset(ControlRegion, TEST_PATTERN2, TEST_SIZE);
        memset(MembaseTop, TEST_PATTERN2, TEST_SIZE);
        __wbinvd();
        if (memcmp(MembaseTop, ControlRegion, TEST_SIZE) == 0)
        {
            /* Definitely looks like there is memory */
            if (memcmp(MembaseLow, ControlRegion, TEST_SIZE) == 0)
            {
                /* Hell, we find the Test-string at 0x0 too! */
                InstalledMemoryMb = 64;
            }
            else
            {
                InstalledMemoryMb = 128;
            }
        }
    }

    /* Set hardware for amount of memory detected */
    WRITE_PORT_ULONG((PULONG)0xCF8, CONFIG_CMD(0, 0, 0x84));
    WRITE_PORT_ULONG((PULONG)0xCFC, InstalledMemoryMb * 1024 * 1024 - 1);

    AvailableMemoryMb = InstalledMemoryMb;
}

memory_map_t *
XboxGetMultibootMemoryMap(INT * Count)
{
    memory_map_t * MemoryMap;

    if (!MultibootInfoPtr)
    {
        ERR("Multiboot info structure not found!\n");
        return NULL;
    }

    if (!(MultibootInfoPtr->flags & MB_INFO_FLAG_MEMORY_MAP))
    {
        ERR("Multiboot memory map is not passed!\n");
        return NULL;
    }

    MemoryMap = (memory_map_t *)MultibootInfoPtr->mmap_addr;

    if (!MemoryMap ||
        MultibootInfoPtr->mmap_length == 0 ||
        MultibootInfoPtr->mmap_length % sizeof(memory_map_t) != 0)
    {
        ERR("Multiboot memory map structure is malformed!\n");
        return NULL;
    }

    *Count = MultibootInfoPtr->mmap_length / sizeof(memory_map_t);
    return MemoryMap;
}

TYPE_OF_MEMORY
XboxMultibootMemoryType(ULONG Type)
{
    switch (Type)
    {
        case 0: // Video RAM
            return LoaderFirmwarePermanent;
        case 1: // Available RAM
            return LoaderFree;
        case 3: // ACPI area
            return LoaderFirmwareTemporary;
        case 4: // Hibernation area
            return LoaderSpecialMemory;
        case 5: // Reserved or invalid memory
            return LoaderSpecialMemory;
        default:
            return LoaderFirmwarePermanent;
    }
}

FREELDR_MEMORY_DESCRIPTOR XboxMemoryMap[MAX_BIOS_DESCRIPTORS + 1];

PFREELDR_MEMORY_DESCRIPTOR
XboxMemGetMemoryMap(ULONG *MemoryMapSize)
{
    memory_map_t * MbMap;
    INT Count, i;

    TRACE("XboxMemGetMemoryMap()\n");

    MbMap = XboxGetMultibootMemoryMap(&Count);
    if (MbMap)
    {
        /* Obtain memory map via multiboot spec */

        for (i = 0; i < Count; i++, MbMap++)
        {
            TRACE("i = %d, base_addr_low = 0x%p, length_low = 0x%p\n", i, MbMap->base_addr_low, MbMap->length_low);

            if (MbMap->base_addr_high > 0 || MbMap->length_high > 0)
            {
                ERR("Memory descriptor base or size is greater than 4 GB, should not happen on Xbox!\n");
                ASSERT(FALSE);
            }

            SetMemory(XboxMemoryMap,
                      MbMap->base_addr_low,
                      MbMap->length_low,
                      XboxMultibootMemoryType(MbMap->type));
        }
    }
    else
    {
        /* Synthesize memory map */

        /* Available RAM block */
        SetMemory(XboxMemoryMap,
                  0,
                  AvailableMemoryMb * 1024 * 1024,
                  LoaderFree);

        if (FrameBufferSize != 0)
        {
            /* Video memory */
            ReserveMemory(XboxMemoryMap,
                          (ULONG_PTR)FrameBuffer,
                          FrameBufferSize,
                          LoaderFirmwarePermanent,
                          "Video memory");
        }
    }

    *MemoryMapSize = PcMemFinalizeMemoryMap(XboxMemoryMap);
    return XboxMemoryMap;
}

/* EOF */
