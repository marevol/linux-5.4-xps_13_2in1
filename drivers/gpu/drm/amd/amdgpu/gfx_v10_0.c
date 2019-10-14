/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_psp.h"
#include "amdgpu_smu.h"
#include "nv.h"
#include "nvd.h"

#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "navi10_enum.h"
#include "hdp/hdp_5_0_0_offset.h"
#include "ivsrcid/gfx/irqsrcs_gfx_10_1.h"

#include "soc15.h"
#include "soc15_common.h"
#include "clearstate_gfx10.h"
#include "v10_structs.h"
#include "gfx_v10_0.h"
#include "nbio_v2_3.h"

/**
 * Navi10 has two graphic rings to share each graphic pipe.
 * 1. Primary ring
 * 2. Async ring
 *
 * In bring-up phase, it just used primary ring so set gfx ring number as 1 at
 * first.
 */
#define GFX10_NUM_GFX_RINGS	2
#define GFX10_MEC_HPD_SIZE	2048

#define F32_CE_PROGRAM_RAM_SIZE		65536
#define RLCG_UCODE_LOADING_START_ADDRESS	0x00002000L

#define mmCGTT_GS_NGG_CLK_CTRL	0x5087
#define mmCGTT_GS_NGG_CLK_CTRL_BASE_IDX	1

MODULE_FIRMWARE("amdgpu/navi10_ce.bin");
MODULE_FIRMWARE("amdgpu/navi10_pfp.bin");
MODULE_FIRMWARE("amdgpu/navi10_me.bin");
MODULE_FIRMWARE("amdgpu/navi10_mec.bin");
MODULE_FIRMWARE("amdgpu/navi10_mec2.bin");
MODULE_FIRMWARE("amdgpu/navi10_rlc.bin");

MODULE_FIRMWARE("amdgpu/navi14_ce_wks.bin");
MODULE_FIRMWARE("amdgpu/navi14_pfp_wks.bin");
MODULE_FIRMWARE("amdgpu/navi14_me_wks.bin");
MODULE_FIRMWARE("amdgpu/navi14_mec_wks.bin");
MODULE_FIRMWARE("amdgpu/navi14_mec2_wks.bin");
MODULE_FIRMWARE("amdgpu/navi14_ce.bin");
MODULE_FIRMWARE("amdgpu/navi14_pfp.bin");
MODULE_FIRMWARE("amdgpu/navi14_me.bin");
MODULE_FIRMWARE("amdgpu/navi14_mec.bin");
MODULE_FIRMWARE("amdgpu/navi14_mec2.bin");
MODULE_FIRMWARE("amdgpu/navi14_rlc.bin");

MODULE_FIRMWARE("amdgpu/navi12_ce.bin");
MODULE_FIRMWARE("amdgpu/navi12_pfp.bin");
MODULE_FIRMWARE("amdgpu/navi12_me.bin");
MODULE_FIRMWARE("amdgpu/navi12_mec.bin");
MODULE_FIRMWARE("amdgpu/navi12_mec2.bin");
MODULE_FIRMWARE("amdgpu/navi12_rlc.bin");

static const struct soc15_reg_golden golden_settings_gc_10_1[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_4, 0xffffffff, 0x00400014),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_CPF_CLK_CTRL, 0xfcff8fff, 0xf8000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SPI_CLK_CTRL, 0xc0000000, 0xc0000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQ_CLK_CTRL, 0x60000ff0, 0x60000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQG_CLK_CTRL, 0x40000000, 0x40000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_VGT_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_WD_CLK_CTRL, 0xfeff8fff, 0xfeff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_VC5_ENABLE, 0x00000002, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_SD_CNTL, 0x000007ff, 0x000005ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG, 0x20000000, 0x20000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xffffffff, 0x00000420),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG3, 0x00000200, 0x00000200),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG4, 0x07900000, 0x04900000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DFSM_TILES_IN_FLIGHT, 0x0000ffff, 0x0000003f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_LAST_OF_BURST_CONFIG, 0xffffffff, 0x03860204),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGCR_GENERAL_CNTL, 0x1ff0ffff, 0x00000500),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGE_PRIV_CONTROL, 0x000007ff, 0x000001fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL1_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2_PIPE_STEER_0, 0x77777777, 0x10321032),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2_PIPE_STEER_1, 0x77777777, 0x02310231),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2A_ADDR_MATCH_MASK, 0xffffffff, 0xffffffcf),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_ADDR_MATCH_MASK, 0xffffffff, 0xffffffcf),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CGTT_SCLK_CTRL, 0x10000000, 0x10000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL2, 0xffffffff, 0x1402002f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL3, 0xffff9fff, 0x00001188),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x08000009),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0x00400000, 0x04440000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_SPARE, 0xffffffff, 0xffff3101),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ALU_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ARB_CONFIG, 0x00000100, 0x00000130),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_LDS_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfff7ffff, 0x01030000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CNTL, 0x60000010, 0x479c0010),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmUTCL1_CGTT_CLK_CTRL, 0xfeff0fff, 0x40000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmUTCL1_CTRL, 0x00800000, 0x00800000)
};

static const struct soc15_reg_golden golden_settings_gc_10_0_nv10[] =
{
	/* Pending on emulation bring up */
};

static const struct soc15_reg_golden golden_settings_gc_10_1_1[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_4, 0xffffffff, 0x003c0014),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_GS_NGG_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_IA_CLK_CTRL, 0xffff0fff, 0xffff0100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SPI_CLK_CTRL, 0xc0000000, 0xc0000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQ_CLK_CTRL, 0xf8ff0fff, 0x60000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQG_CLK_CTRL, 0x40000ff0, 0x40000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_VGT_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_WD_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_VC5_ENABLE, 0x00000002, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_SD_CNTL, 0x800007ff, 0x000005ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG, 0xffffffff, 0x20000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xffffffff, 0x00000420),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG3, 0x00000200, 0x00000200),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG4, 0xffffffff, 0x04900000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DFSM_TILES_IN_FLIGHT, 0x0000ffff, 0x0000003f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_LAST_OF_BURST_CONFIG, 0xffffffff, 0x03860204),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGCR_GENERAL_CNTL, 0x1ff0ffff, 0x00000500),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGE_PRIV_CONTROL, 0x000007ff, 0x000001fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL1_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2A_ADDR_MATCH_MASK, 0xffffffff, 0xffffffe7),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_ADDR_MATCH_MASK, 0xffffffff, 0xffffffe7),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CGTT_SCLK_CTRL, 0xffff0fff, 0x10000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL2, 0xffffffff, 0x1402002f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL3, 0xffffbfff, 0x00000188),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x08000009),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0x00400000, 0x04440000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_SPARE, 0xffffffff, 0xffff3101),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ALU_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ARB_CONFIG, 0x00000133, 0x00000130),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_LDS_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfff7ffff, 0x01030000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CNTL, 0x60000010, 0x479c0010),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmUTCL1_CTRL, 0x00800000, 0x00800000),
};

static const struct soc15_reg_golden golden_settings_gc_10_1_2[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_4, 0x003e001f, 0x003c0014),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_GS_NGG_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_IA_CLK_CTRL, 0xffff0fff, 0xffff0100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SPI_CLK_CTRL, 0xff7f0fff, 0xc0000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQ_CLK_CTRL, 0xffffcfff, 0x60000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_SQG_CLK_CTRL, 0xffff0fff, 0x40000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_VGT_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCGTT_WD_CLK_CTRL, 0xffff8fff, 0xffff8100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCH_VC5_ENABLE, 0x00000003, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_SD_CNTL, 0x800007ff, 0x000005ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG, 0xffffffff, 0x20000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xffffffff, 0x00000420),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG3, 0xffffffff, 0x00000200),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG4, 0xffffffff, 0x04800000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DFSM_TILES_IN_FLIGHT, 0x0000ffff, 0x0000003f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_LAST_OF_BURST_CONFIG, 0xffffffff, 0x03860204),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGCR_GENERAL_CNTL, 0x1ff0ffff, 0x00000500),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGE_PRIV_CONTROL, 0x00007fff, 0x000001fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL1_PIPE_STEER, 0xffffffff, 0xe4e4e4e4),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2_PIPE_STEER_0, 0x77777777, 0x10321032),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2_PIPE_STEER_1, 0x77777777, 0x02310231),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2A_ADDR_MATCH_MASK, 0xffffffff, 0xffffffcf),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_ADDR_MATCH_MASK, 0xffffffff, 0xffffffcf),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CGTT_SCLK_CTRL, 0xffff0fff, 0x10000100),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL2, 0xffffffff, 0x1402002f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGL2C_CTRL3, 0xffffbfff, 0x00000188),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_BINNER_EVENT_CNTL_0, 0xffffffff, 0x842a4c02),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_BINNER_TIMEOUT_COUNTER, 0xffffffff, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x08000009),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0xffffffff, 0x04440000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_2, 0x00000820, 0x00000820),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_SPARE, 0xffffffff, 0xffff3101),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ALU_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_ARB_CONFIG, 0x00000133, 0x00000130),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQ_LDS_CLK_CTRL, 0xffffffff, 0xffffffff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfff7ffff, 0x01030000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CNTL, 0xffdf80ff, 0x479c0010),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmUTCL1_CTRL, 0xffffffff, 0x00800000)
};

static const struct soc15_reg_golden golden_settings_gc_10_1_nv14[] =
{
	/* Pending on emulation bring up */
};

static const struct soc15_reg_golden golden_settings_gc_10_1_2_nv12[] =
{
	/* Pending on emulation bring up */
};

#define DEFAULT_SH_MEM_CONFIG \
	((SH_MEM_ADDRESS_MODE_64 << SH_MEM_CONFIG__ADDRESS_MODE__SHIFT) | \
	 (SH_MEM_ALIGNMENT_MODE_UNALIGNED << SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT) | \
	 (SH_MEM_RETRY_MODE_ALL << SH_MEM_CONFIG__RETRY_MODE__SHIFT) | \
	 (3 << SH_MEM_CONFIG__INITIAL_INST_PREFETCH__SHIFT))


static void gfx_v10_0_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v10_0_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v10_0_set_gds_init(struct amdgpu_device *adev);
static void gfx_v10_0_set_rlc_funcs(struct amdgpu_device *adev);
static int gfx_v10_0_get_cu_info(struct amdgpu_device *adev,
                                 struct amdgpu_cu_info *cu_info);
static uint64_t gfx_v10_0_get_gpu_clock_counter(struct amdgpu_device *adev);
static void gfx_v10_0_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				   u32 sh_num, u32 instance);
static u32 gfx_v10_0_get_wgp_active_bitmap_per_sh(struct amdgpu_device *adev);

static int gfx_v10_0_rlc_backdoor_autoload_buffer_init(struct amdgpu_device *adev);
static void gfx_v10_0_rlc_backdoor_autoload_buffer_fini(struct amdgpu_device *adev);
static int gfx_v10_0_rlc_backdoor_autoload_enable(struct amdgpu_device *adev);
static int gfx_v10_0_wait_for_rlc_autoload_complete(struct amdgpu_device *adev);
static void gfx_v10_0_ring_emit_ce_meta(struct amdgpu_ring *ring, bool resume);
static void gfx_v10_0_ring_emit_de_meta(struct amdgpu_ring *ring, bool resume);
static void gfx_v10_0_ring_emit_tmz(struct amdgpu_ring *ring, bool start);

static void gfx10_kiq_set_resources(struct amdgpu_ring *kiq_ring, uint64_t queue_mask)
{
	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_SET_RESOURCES, 6));
	amdgpu_ring_write(kiq_ring, PACKET3_SET_RESOURCES_VMID_MASK(0) |
			  PACKET3_SET_RESOURCES_QUEUE_TYPE(0));	/* vmid_mask:0 queue_type:0 (KIQ) */
	amdgpu_ring_write(kiq_ring, lower_32_bits(queue_mask));	/* queue mask lo */
	amdgpu_ring_write(kiq_ring, upper_32_bits(queue_mask));	/* queue mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask lo */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* oac mask */
	amdgpu_ring_write(kiq_ring, 0);	/* gds heap base:0, gds heap size:0 */
}

static void gfx10_kiq_map_queues(struct amdgpu_ring *kiq_ring,
				 struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = kiq_ring->adev;
	uint64_t mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
	uint64_t wptr_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
	/* Q_sel:0, vmid:0, vidmem: 1, engine:0, num_Q:1*/
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
			  PACKET3_MAP_QUEUES_VMID(0) | /* VMID */
			  PACKET3_MAP_QUEUES_QUEUE(ring->queue) |
			  PACKET3_MAP_QUEUES_PIPE(ring->pipe) |
			  PACKET3_MAP_QUEUES_ME((ring->me == 1 ? 0 : 1)) |
			  PACKET3_MAP_QUEUES_QUEUE_TYPE(0) | /*queue_type: normal compute queue */
			  PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) | /* alloc format: all_on_one_pipe */
			  PACKET3_MAP_QUEUES_ENGINE_SEL(eng_sel) |
			  PACKET3_MAP_QUEUES_NUM_QUEUES(1)); /* num_queues: must be 1 */
	amdgpu_ring_write(kiq_ring, PACKET3_MAP_QUEUES_DOORBELL_OFFSET(ring->doorbell_index));
	amdgpu_ring_write(kiq_ring, lower_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(mqd_addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(wptr_addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(wptr_addr));
}

static void gfx10_kiq_unmap_queues(struct amdgpu_ring *kiq_ring,
				   struct amdgpu_ring *ring,
				   enum amdgpu_unmap_queues_action action,
				   u64 gpu_addr, u64 seq)
{
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_UNMAP_QUEUES, 4));
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_UNMAP_QUEUES_ACTION(action) |
			  PACKET3_UNMAP_QUEUES_QUEUE_SEL(0) |
			  PACKET3_UNMAP_QUEUES_ENGINE_SEL(eng_sel) |
			  PACKET3_UNMAP_QUEUES_NUM_QUEUES(1));
	amdgpu_ring_write(kiq_ring,
		  PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(ring->doorbell_index));

	if (action == PREEMPT_QUEUES_NO_UNMAP) {
		amdgpu_ring_write(kiq_ring, lower_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(gpu_addr));
		amdgpu_ring_write(kiq_ring, seq);
	} else {
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
	}
}

static void gfx10_kiq_query_status(struct amdgpu_ring *kiq_ring,
				   struct amdgpu_ring *ring,
				   u64 addr,
				   u64 seq)
{
	uint32_t eng_sel = ring->funcs->type == AMDGPU_RING_TYPE_GFX ? 4 : 0;

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_QUERY_STATUS, 5));
	amdgpu_ring_write(kiq_ring,
			  PACKET3_QUERY_STATUS_CONTEXT_ID(0) |
			  PACKET3_QUERY_STATUS_INTERRUPT_SEL(0) |
			  PACKET3_QUERY_STATUS_COMMAND(2));
	amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
			  PACKET3_QUERY_STATUS_DOORBELL_OFFSET(ring->doorbell_index) |
			  PACKET3_QUERY_STATUS_ENG_SEL(eng_sel));
	amdgpu_ring_write(kiq_ring, lower_32_bits(addr));
	amdgpu_ring_write(kiq_ring, upper_32_bits(addr));
	amdgpu_ring_write(kiq_ring, lower_32_bits(seq));
	amdgpu_ring_write(kiq_ring, upper_32_bits(seq));
}

static const struct kiq_pm4_funcs gfx_v10_0_kiq_pm4_funcs = {
	.kiq_set_resources = gfx10_kiq_set_resources,
	.kiq_map_queues = gfx10_kiq_map_queues,
	.kiq_unmap_queues = gfx10_kiq_unmap_queues,
	.kiq_query_status = gfx10_kiq_query_status,
	.set_resources_size = 8,
	.map_queues_size = 7,
	.unmap_queues_size = 6,
	.query_status_size = 7,
};

static void gfx_v10_0_set_kiq_pm4_funcs(struct amdgpu_device *adev)
{
	adev->gfx.kiq.pmf = &gfx_v10_0_kiq_pm4_funcs;
}

static void gfx_v10_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_NAVI10:
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_1,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_1));
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_0_nv10,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_0_nv10));
		break;
	case CHIP_NAVI14:
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_1_1,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_1_1));
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_1_nv14,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_1_nv14));
		break;
	case CHIP_NAVI12:
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_1_2,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_1_2));
		soc15_program_register_sequence(adev,
						golden_settings_gc_10_1_2_nv12,
						(const u32)ARRAY_SIZE(golden_settings_gc_10_1_2_nv12));
		break;
	default:
		break;
	}
}

static void gfx_v10_0_scratch_init(struct amdgpu_device *adev)
{
	adev->gfx.scratch.num_reg = 8;
	adev->gfx.scratch.reg_base = SOC15_REG_OFFSET(GC, 0, mmSCRATCH_REG0);
	adev->gfx.scratch.free_mask = (1u << adev->gfx.scratch.num_reg) - 1;
}

static void gfx_v10_0_write_data_to_reg(struct amdgpu_ring *ring, int eng_sel,
				       bool wc, uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, WRITE_DATA_ENGINE_SEL(eng_sel) |
			  WRITE_DATA_DST_SEL(0) | (wc ? WR_CONFIRM : 0));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v10_0_wait_reg_mem(struct amdgpu_ring *ring, int eng_sel,
				  int mem_space, int opt, uint32_t addr0,
				  uint32_t addr1, uint32_t ref, uint32_t mask,
				  uint32_t inv)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring,
			  /* memory (1) or register (0) */
			  (WAIT_REG_MEM_MEM_SPACE(mem_space) |
			   WAIT_REG_MEM_OPERATION(opt) | /* wait */
			   WAIT_REG_MEM_FUNCTION(3) |  /* equal */
			   WAIT_REG_MEM_ENGINE(eng_sel)));

	if (mem_space)
		BUG_ON(addr0 & 0x3); /* Dword align */
	amdgpu_ring_write(ring, addr0);
	amdgpu_ring_write(ring, addr1);
	amdgpu_ring_write(ring, ref);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, inv); /* poll interval */
}

static int gfx_v10_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to get scratch reg (%d).\n", r);
		return r;
	}

	WREG32(scratch, 0xCAFEDEAD);

	r = amdgpu_ring_alloc(ring, 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n",
			  ring->idx, r);
		amdgpu_gfx_scratch_free(adev, scratch);
		return r;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	amdgpu_ring_write(ring, (scratch - PACKET3_SET_UCONFIG_REG_START));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		if (amdgpu_emu_mode == 1)
			msleep(1);
		else
			udelay(1);
	}
	if (i < adev->usec_timeout) {
		if (amdgpu_emu_mode == 1)
			DRM_INFO("ring test on %d succeeded in %d msecs\n",
				 ring->idx, i);
		else
			DRM_INFO("ring test on %d succeeded in %d usecs\n",
				 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (scratch(0x%04X)=0x%08X)\n",
			  ring->idx, scratch, tmp);
		r = -EINVAL;
	}
	amdgpu_gfx_scratch_free(adev, scratch);

	return r;
}

static int gfx_v10_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	uint32_t scratch;
	uint32_t tmp = 0;
	long r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: failed to get scratch reg (%ld).\n", r);
		return r;
	}

	WREG32(scratch, 0xCAFEDEAD);

	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
		goto err1;
	}

	ib.ptr[0] = PACKET3(PACKET3_SET_UCONFIG_REG, 1);
	ib.ptr[1] = ((scratch - PACKET3_SET_UCONFIG_REG_START));
	ib.ptr[2] = 0xDEADBEEF;
	ib.length_dw = 3;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err2;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out.\n");
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto err2;
	}

	tmp = RREG32(scratch);
	if (tmp == 0xDEADBEEF) {
		DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	} else {
		DRM_ERROR("amdgpu: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
err2:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err1:
	amdgpu_gfx_scratch_free(adev, scratch);

	return r;
}

static void gfx_v10_0_free_microcode(struct amdgpu_device *adev)
{
	release_firmware(adev->gfx.pfp_fw);
	adev->gfx.pfp_fw = NULL;
	release_firmware(adev->gfx.me_fw);
	adev->gfx.me_fw = NULL;
	release_firmware(adev->gfx.ce_fw);
	adev->gfx.ce_fw = NULL;
	release_firmware(adev->gfx.rlc_fw);
	adev->gfx.rlc_fw = NULL;
	release_firmware(adev->gfx.mec_fw);
	adev->gfx.mec_fw = NULL;
	release_firmware(adev->gfx.mec2_fw);
	adev->gfx.mec2_fw = NULL;

	kfree(adev->gfx.rlc.register_list_format);
}

static void gfx_v10_0_init_rlc_ext_microcode(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_1 *rlc_hdr;

	rlc_hdr = (const struct rlc_firmware_header_v2_1 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_srlc_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_ucode_ver);
	adev->gfx.rlc_srlc_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_feature_ver);
	adev->gfx.rlc.save_restore_list_cntl_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_cntl_size_bytes);
	adev->gfx.rlc.save_restore_list_cntl = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_cntl_offset_bytes);
	adev->gfx.rlc_srlg_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_ucode_ver);
	adev->gfx.rlc_srlg_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_feature_ver);
	adev->gfx.rlc.save_restore_list_gpm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_gpm_size_bytes);
	adev->gfx.rlc.save_restore_list_gpm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_gpm_offset_bytes);
	adev->gfx.rlc_srls_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_ucode_ver);
	adev->gfx.rlc_srls_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_feature_ver);
	adev->gfx.rlc.save_restore_list_srm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_srm_size_bytes);
	adev->gfx.rlc.save_restore_list_srm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_srm_offset_bytes);
	adev->gfx.rlc.reg_list_format_direct_reg_list_length =
			le32_to_cpu(rlc_hdr->reg_list_format_direct_reg_list_length);
}

static void gfx_v10_0_check_gfxoff_flag(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_NAVI10:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		break;
	default:
		break;
	}
}

static int gfx_v10_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[40];
	char wks[10];
	int err;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	unsigned int *tmp = NULL;
	unsigned int i = 0;
	uint16_t version_major;
	uint16_t version_minor;

	DRM_DEBUG("\n");

	memset(wks, 0, sizeof(wks));
	switch (adev->asic_type) {
	case CHIP_NAVI10:
		chip_name = "navi10";
		break;
	case CHIP_NAVI14:
		chip_name = "navi14";
		if (!(adev->pdev->device == 0x7340 &&
		      adev->pdev->revision != 0x00))
			snprintf(wks, sizeof(wks), "_wks");
		break;
	case CHIP_NAVI12:
		chip_name = "navi12";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_pfp%s.bin", chip_name, wks);
	err = request_firmware(&adev->gfx.pfp_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.pfp_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
	adev->gfx.pfp_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.pfp_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_me%s.bin", chip_name, wks);
	err = request_firmware(&adev->gfx.me_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.me_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
	adev->gfx.me_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.me_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ce%s.bin", chip_name, wks);
	err = request_firmware(&adev->gfx.ce_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.ce_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
	adev->gfx.ce_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.ce_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_rlc.bin", chip_name);
	err = request_firmware(&adev->gfx.rlc_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.rlc_fw);
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	version_major = le16_to_cpu(rlc_hdr->header.header_version_major);
	version_minor = le16_to_cpu(rlc_hdr->header.header_version_minor);
	if (version_major == 2 && version_minor == 1)
		adev->gfx.rlc.is_rlc_v2_1 = true;

	adev->gfx.rlc_fw_version = le32_to_cpu(rlc_hdr->header.ucode_version);
	adev->gfx.rlc_feature_version = le32_to_cpu(rlc_hdr->ucode_feature_version);
	adev->gfx.rlc.save_and_restore_offset =
			le32_to_cpu(rlc_hdr->save_and_restore_offset);
	adev->gfx.rlc.clear_state_descriptor_offset =
			le32_to_cpu(rlc_hdr->clear_state_descriptor_offset);
	adev->gfx.rlc.avail_scratch_ram_locations =
			le32_to_cpu(rlc_hdr->avail_scratch_ram_locations);
	adev->gfx.rlc.reg_restore_list_size =
			le32_to_cpu(rlc_hdr->reg_restore_list_size);
	adev->gfx.rlc.reg_list_format_start =
			le32_to_cpu(rlc_hdr->reg_list_format_start);
	adev->gfx.rlc.reg_list_format_separate_start =
			le32_to_cpu(rlc_hdr->reg_list_format_separate_start);
	adev->gfx.rlc.starting_offsets_start =
			le32_to_cpu(rlc_hdr->starting_offsets_start);
	adev->gfx.rlc.reg_list_format_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_format_size_bytes);
	adev->gfx.rlc.reg_list_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_size_bytes);
	adev->gfx.rlc.register_list_format =
			kmalloc(adev->gfx.rlc.reg_list_format_size_bytes +
				adev->gfx.rlc.reg_list_size_bytes, GFP_KERNEL);
	if (!adev->gfx.rlc.register_list_format) {
		err = -ENOMEM;
		goto out;
	}

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_format_size_bytes >> 2); i++)
		adev->gfx.rlc.register_list_format[i] =	le32_to_cpu(tmp[i]);

	adev->gfx.rlc.register_restore = adev->gfx.rlc.register_list_format + i;

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_size_bytes >> 2); i++)
		adev->gfx.rlc.register_restore[i] = le32_to_cpu(tmp[i]);

	if (adev->gfx.rlc.is_rlc_v2_1)
		gfx_v10_0_init_rlc_ext_microcode(adev);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mec%s.bin", chip_name, wks);
	err = request_firmware(&adev->gfx.mec_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.mec_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	adev->gfx.mec_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.mec_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mec2%s.bin", chip_name, wks);
	err = request_firmware(&adev->gfx.mec2_fw, fw_name, adev->dev);
	if (!err) {
		err = amdgpu_ucode_validate(adev->gfx.mec2_fw);
		if (err)
			goto out;
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.mec2_fw->data;
		adev->gfx.mec2_fw_version =
		le32_to_cpu(cp_hdr->header.ucode_version);
		adev->gfx.mec2_feature_version =
		le32_to_cpu(cp_hdr->ucode_feature_version);
	} else {
		err = 0;
		adev->gfx.mec2_fw = NULL;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_PFP];
		info->ucode_id = AMDGPU_UCODE_ID_CP_PFP;
		info->fw = adev->gfx.pfp_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_ME];
		info->ucode_id = AMDGPU_UCODE_ID_CP_ME;
		info->fw = adev->gfx.me_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_CE];
		info->ucode_id = AMDGPU_UCODE_ID_CP_CE;
		info->fw = adev->gfx.ce_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_G];
		info->ucode_id = AMDGPU_UCODE_ID_RLC_G;
		info->fw = adev->gfx.rlc_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		if (adev->gfx.rlc.is_rlc_v2_1 &&
		    adev->gfx.rlc.save_restore_list_cntl_size_bytes &&
		    adev->gfx.rlc.save_restore_list_gpm_size_bytes &&
		    adev->gfx.rlc.save_restore_list_srm_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_cntl_size_bytes, PAGE_SIZE);

			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_gpm_size_bytes, PAGE_SIZE);

			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_srm_size_bytes, PAGE_SIZE);
		}

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1;
		info->fw = adev->gfx.mec_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes) -
			      le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1_JT];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1_JT;
		info->fw = adev->gfx.mec_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);

		if (adev->gfx.mec2_fw) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2];
			info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2;
			info->fw = adev->gfx.mec2_fw;
			header = (const struct common_firmware_header *)info->fw->data;
			cp_hdr = (const struct gfx_firmware_header_v1_0 *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(header->ucode_size_bytes) -
				      le32_to_cpu(cp_hdr->jt_size) * 4,
				      PAGE_SIZE);
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2_JT];
			info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2_JT;
			info->fw = adev->gfx.mec2_fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(cp_hdr->jt_size) * 4,
				      PAGE_SIZE);
		}
	}

out:
	if (err) {
		dev_err(adev->dev,
			"gfx10: Failed to load firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->gfx.pfp_fw);
		adev->gfx.pfp_fw = NULL;
		release_firmware(adev->gfx.me_fw);
		adev->gfx.me_fw = NULL;
		release_firmware(adev->gfx.ce_fw);
		adev->gfx.ce_fw = NULL;
		release_firmware(adev->gfx.rlc_fw);
		adev->gfx.rlc_fw = NULL;
		release_firmware(adev->gfx.mec_fw);
		adev->gfx.mec_fw = NULL;
		release_firmware(adev->gfx.mec2_fw);
		adev->gfx.mec2_fw = NULL;
	}

	gfx_v10_0_check_gfxoff_flag(adev);

	return err;
}

static u32 gfx_v10_0_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = gfx10_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}

	/* set PA_SC_TILE_STEERING_OVERRIDE */
	count += 3;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

static void gfx_v10_0_get_csb_buffer(struct amdgpu_device *adev,
				    volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	int ctx_reg_offset;

	if (adev->gfx.rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index -
						PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	ctx_reg_offset =
		SOC15_REG_OFFSET(GC, 0, mmPA_SC_TILE_STEERING_OVERRIDE) - PACKET3_SET_CONTEXT_REG_START;
	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	buffer[count++] = cpu_to_le32(ctx_reg_offset);
	buffer[count++] = cpu_to_le32(adev->gfx.config.pa_sc_tile_steering_override);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void gfx_v10_0_rlc_fini(struct amdgpu_device *adev)
{
	/* clear state block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.clear_state_obj,
			&adev->gfx.rlc.clear_state_gpu_addr,
			(void **)&adev->gfx.rlc.cs_ptr);

	/* jump table block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
			&adev->gfx.rlc.cp_table_gpu_addr,
			(void **)&adev->gfx.rlc.cp_table_ptr);
}

static int gfx_v10_0_rlc_init(struct amdgpu_device *adev)
{
	const struct cs_section_def *cs_data;
	int r;

	adev->gfx.rlc.cs_data = gfx10_cs_data;

	cs_data = adev->gfx.rlc.cs_data;

	if (cs_data) {
		/* init clear state block */
		r = amdgpu_gfx_rlc_init_csb(adev);
		if (r)
			return r;
	}

	return 0;
}

static int gfx_v10_0_csb_vram_pin(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_pin(adev->gfx.rlc.clear_state_obj,
			AMDGPU_GEM_DOMAIN_VRAM);
	if (!r)
		adev->gfx.rlc.clear_state_gpu_addr =
			amdgpu_bo_gpu_offset(adev->gfx.rlc.clear_state_obj);

	amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);

	return r;
}

static void gfx_v10_0_csb_vram_unpin(struct amdgpu_device *adev)
{
	int r;

	if (!adev->gfx.rlc.clear_state_obj)
		return;

	r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, true);
	if (likely(r == 0)) {
		amdgpu_bo_unpin(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
	}
}

static void gfx_v10_0_mec_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.mec.hpd_eop_obj, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->gfx.mec.mec_fw_obj, NULL, NULL);
}

static int gfx_v10_0_me_init(struct amdgpu_device *adev)
{
	int r;

	bitmap_zero(adev->gfx.me.queue_bitmap, AMDGPU_MAX_GFX_QUEUES);

	amdgpu_gfx_graphics_queue_acquire(adev);

	r = gfx_v10_0_init_microcode(adev);
	if (r)
		DRM_ERROR("Failed to load gfx firmware!\n");

	return r;
}

static int gfx_v10_0_mec_init(struct amdgpu_device *adev)
{
	int r;
	u32 *hpd;
	const __le32 *fw_data = NULL;
	unsigned fw_size;
	u32 *fw = NULL;
	size_t mec_hpd_size;

	const struct gfx_firmware_header_v1_0 *mec_hdr = NULL;

	bitmap_zero(adev->gfx.mec.queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/* take ownership of the relevant compute queues */
	amdgpu_gfx_compute_queue_acquire(adev);
	mec_hpd_size = adev->gfx.num_compute_rings * GFX10_MEC_HPD_SIZE;

	r = amdgpu_bo_create_reserved(adev, mec_hpd_size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.mec.hpd_eop_obj,
				      &adev->gfx.mec.hpd_eop_gpu_addr,
				      (void **)&hpd);
	if (r) {
		dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
		gfx_v10_0_mec_fini(adev);
		return r;
	}

	memset(hpd, 0, adev->gfx.mec.hpd_eop_obj->tbo.mem.size);

	amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
	amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;

		fw_data = (const __le32 *) (adev->gfx.mec_fw->data +
			 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(mec_hdr->header.ucode_size_bytes);

		r = amdgpu_bo_create_reserved(adev, mec_hdr->header.ucode_size_bytes,
					      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
					      &adev->gfx.mec.mec_fw_obj,
					      &adev->gfx.mec.mec_fw_gpu_addr,
					      (void **)&fw);
		if (r) {
			dev_err(adev->dev, "(%d) failed to create mec fw bo\n", r);
			gfx_v10_0_mec_fini(adev);
			return r;
		}

		memcpy(fw, fw_data, fw_size);

		amdgpu_bo_kunmap(adev->gfx.mec.mec_fw_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.mec_fw_obj);
	}

	return 0;
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t wave, uint32_t address)
{
	WREG32_SOC15(GC, 0, mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT));
	return RREG32_SOC15(GC, 0, mmSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev, uint32_t wave,
			   uint32_t thread, uint32_t regno,
			   uint32_t num, uint32_t *out)
{
	WREG32_SOC15(GC, 0, mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__WORKITEM_ID__SHIFT) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32_SOC15(GC, 0, mmSQ_IND_DATA);
}

static void gfx_v10_0_read_wave_data(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t *dst, int *no_fields)
{
	/* in gfx10 the SIMD_ID is specified as part of the INSTANCE
	 * field when performing a select_se_sh so it should be
	 * zero here */
	WARN_ON(simd != 0);

	/* type 2 wave data */
	dst[(*no_fields)++] = 2;
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_HW_ID1);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_HW_ID2);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_INST_DW0);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_TRAPSTS);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_IB_STS2);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_IB_DBG1);
	dst[(*no_fields)++] = wave_read_ind(adev, wave, ixSQ_WAVE_M0);
}

static void gfx_v10_0_read_wave_sgprs(struct amdgpu_device *adev, uint32_t simd,
				     uint32_t wave, uint32_t start,
				     uint32_t size, uint32_t *dst)
{
	WARN_ON(simd != 0);

	wave_read_regs(
		adev, wave, 0, start + SQIND_WAVE_SGPRS_OFFSET, size,
		dst);
}

static void gfx_v10_0_read_wave_vgprs(struct amdgpu_device *adev, uint32_t simd,
				      uint32_t wave, uint32_t thread,
				      uint32_t start, uint32_t size,
				      uint32_t *dst)
{
	wave_read_regs(
		adev, wave, thread,
		start + SQIND_WAVE_VGPRS_OFFSET, size, dst);
}

static void gfx_v10_0_select_me_pipe_q(struct amdgpu_device *adev,
									  u32 me, u32 pipe, u32 q, u32 vm)
 {
       nv_grbm_select(adev, me, pipe, q, vm);
 }


static const struct amdgpu_gfx_funcs gfx_v10_0_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v10_0_get_gpu_clock_counter,
	.select_se_sh = &gfx_v10_0_select_se_sh,
	.read_wave_data = &gfx_v10_0_read_wave_data,
	.read_wave_sgprs = &gfx_v10_0_read_wave_sgprs,
	.read_wave_vgprs = &gfx_v10_0_read_wave_vgprs,
	.select_me_pipe_q = &gfx_v10_0_select_me_pipe_q,
};

static void gfx_v10_0_gpu_early_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config;

	adev->gfx.funcs = &gfx_v10_0_gfx_funcs;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		gb_addr_config = RREG32_SOC15(GC, 0, mmGB_ADDR_CONFIG);
		break;
	default:
		BUG();
		break;
	}

	adev->gfx.config.gb_addr_config = gb_addr_config;

	adev->gfx.config.gb_addr_config_fields.num_pipes = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG, NUM_PIPES);

	adev->gfx.config.max_tile_pipes =
		adev->gfx.config.gb_addr_config_fields.num_pipes;

	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG, MAX_COMPRESSED_FRAGS);
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG, NUM_RB_PER_SE);
	adev->gfx.config.gb_addr_config_fields.num_se = 1 <<
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG, NUM_SHADER_ENGINES);
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 1 << (8 +
			REG_GET_FIELD(adev->gfx.config.gb_addr_config,
				      GB_ADDR_CONFIG, PIPE_INTERLEAVE_SIZE));
}

static int gfx_v10_0_gfx_ring_init(struct amdgpu_device *adev, int ring_id,
				   int me, int pipe, int queue)
{
	int r;
	struct amdgpu_ring *ring;
	unsigned int irq_type;

	ring = &adev->gfx.gfx_ring[ring_id];

	ring->me = me;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;

	if (!ring_id)
		ring->doorbell_index = adev->doorbell_index.gfx_ring0 << 1;
	else
		ring->doorbell_index = adev->doorbell_index.gfx_ring1 << 1;
	sprintf(ring->name, "gfx_%d.%d.%d", ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP + ring->pipe;
	r = amdgpu_ring_init(adev, ring, 1024,
			     &adev->gfx.eop_irq, irq_type);
	if (r)
		return r;
	return 0;
}

static int gfx_v10_0_compute_ring_init(struct amdgpu_device *adev, int ring_id,
				       int mec, int pipe, int queue)
{
	int r;
	unsigned irq_type;
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[ring_id];

	ring = &adev->gfx.compute_ring[ring_id];

	/* mec0 is me1 */
	ring->me = mec + 1;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = (adev->doorbell_index.mec_ring0 + ring_id) << 1;
	ring->eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr
				+ (ring_id * GFX10_MEC_HPD_SIZE);
	sprintf(ring->name, "comp_%d.%d.%d", ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP
		+ ((ring->me - 1) * adev->gfx.mec.num_pipe_per_mec)
		+ ring->pipe;

	/* type-2 packets are deprecated on MEC, use type-3 instead */
	r = amdgpu_ring_init(adev, ring, 1024,
			     &adev->gfx.eop_irq, irq_type);
	if (r)
		return r;

	return 0;
}

static int gfx_v10_0_sw_init(void *handle)
{
	int i, j, k, r, ring_id = 0;
	struct amdgpu_kiq *kiq;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		adev->gfx.me.num_me = 1;
		adev->gfx.me.num_pipe_per_me = 2;
		adev->gfx.me.num_queue_per_pipe = 1;
		adev->gfx.mec.num_mec = 2;
		adev->gfx.mec.num_pipe_per_mec = 4;
		adev->gfx.mec.num_queue_per_pipe = 8;
		break;
	default:
		adev->gfx.me.num_me = 1;
		adev->gfx.me.num_pipe_per_me = 1;
		adev->gfx.me.num_queue_per_pipe = 1;
		adev->gfx.mec.num_mec = 1;
		adev->gfx.mec.num_pipe_per_mec = 4;
		adev->gfx.mec.num_queue_per_pipe = 8;
		break;
	}

	/* KIQ event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP,
			      GFX_10_1__SRCID__CP_IB2_INTERRUPT_PKT,
			      &adev->gfx.kiq.irq);
	if (r)
		return r;

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP,
			      GFX_10_1__SRCID__CP_EOP_INTERRUPT,
			      &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_10_1__SRCID__CP_PRIV_REG_FAULT,
			      &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_10_1__SRCID__CP_PRIV_INSTR_FAULT,
			      &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	gfx_v10_0_scratch_init(adev);

	r = gfx_v10_0_me_init(adev);
	if (r)
		return r;

	r = gfx_v10_0_rlc_init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v10_0_mec_init(adev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the gfx ring */
	for (i = 0; i < adev->gfx.me.num_me; i++) {
		for (j = 0; j < adev->gfx.me.num_queue_per_pipe; j++) {
			for (k = 0; k < adev->gfx.me.num_pipe_per_me; k++) {
				if (!amdgpu_gfx_is_me_queue_enabled(adev, i, k, j))
					continue;

				r = gfx_v10_0_gfx_ring_init(adev, ring_id,
							    i, k, j);
				if (r)
					return r;
				ring_id++;
			}
		}
	}

	ring_id = 0;
	/* set up the compute queues - allocate horizontally across pipes */
	for (i = 0; i < adev->gfx.mec.num_mec; ++i) {
		for (j = 0; j < adev->gfx.mec.num_queue_per_pipe; j++) {
			for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
				if (!amdgpu_gfx_is_mec_queue_enabled(adev, i, k,
								     j))
					continue;

				r = gfx_v10_0_compute_ring_init(adev, ring_id,
								i, k, j);
				if (r)
					return r;

				ring_id++;
			}
		}
	}

	r = amdgpu_gfx_kiq_init(adev, GFX10_MEC_HPD_SIZE);
	if (r) {
		DRM_ERROR("Failed to init KIQ BOs!\n");
		return r;
	}

	kiq = &adev->gfx.kiq;
	r = amdgpu_gfx_kiq_init_ring(adev, &kiq->ring, &kiq->irq);
	if (r)
		return r;

	r = amdgpu_gfx_mqd_sw_init(adev, sizeof(struct v10_compute_mqd));
	if (r)
		return r;

	/* allocate visible FB for rlc auto-loading fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
		r = gfx_v10_0_rlc_backdoor_autoload_buffer_init(adev);
		if (r)
			return r;
	}

	adev->gfx.ce_ram_size = F32_CE_PROGRAM_RAM_SIZE;

	gfx_v10_0_gpu_early_init(adev);

	return 0;
}

static void gfx_v10_0_pfp_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.pfp.pfp_fw_obj,
			      &adev->gfx.pfp.pfp_fw_gpu_addr,
			      (void **)&adev->gfx.pfp.pfp_fw_ptr);
}

static void gfx_v10_0_ce_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.ce.ce_fw_obj,
			      &adev->gfx.ce.ce_fw_gpu_addr,
			      (void **)&adev->gfx.ce.ce_fw_ptr);
}

static void gfx_v10_0_me_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.me.me_fw_obj,
			      &adev->gfx.me.me_fw_gpu_addr,
			      (void **)&adev->gfx.me.me_fw_ptr);
}

static int gfx_v10_0_sw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		amdgpu_ring_fini(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	amdgpu_gfx_mqd_sw_fini(adev);
	amdgpu_gfx_kiq_free_ring(&adev->gfx.kiq.ring, &adev->gfx.kiq.irq);
	amdgpu_gfx_kiq_fini(adev);

	gfx_v10_0_pfp_fini(adev);
	gfx_v10_0_ce_fini(adev);
	gfx_v10_0_me_fini(adev);
	gfx_v10_0_rlc_fini(adev);
	gfx_v10_0_mec_fini(adev);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO)
		gfx_v10_0_rlc_backdoor_autoload_buffer_fini(adev);

	gfx_v10_0_free_microcode(adev);

	return 0;
}


static void gfx_v10_0_tiling_mode_table_init(struct amdgpu_device *adev)
{
	/* TODO */
}

static void gfx_v10_0_select_se_sh(struct amdgpu_device *adev, u32 se_num,
				   u32 sh_num, u32 instance)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_INDEX,
				     instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SA_BROADCAST_WRITES,
				     1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SA_INDEX, sh_num);

	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);
}

static u32 gfx_v10_0_get_rb_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data = RREG32_SOC15(GC, 0, mmCC_RB_BACKEND_DISABLE);
	data |= RREG32_SOC15(GC, 0, mmGC_USER_RB_BACKEND_DISABLE);

	data &= CC_RB_BACKEND_DISABLE__BACKEND_DISABLE_MASK;
	data >>= GC_USER_RB_BACKEND_DISABLE__BACKEND_DISABLE__SHIFT;

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_backends_per_se /
					 adev->gfx.config.max_sh_per_se);

	return (~data) & mask;
}

static void gfx_v10_0_setup_rb(struct amdgpu_device *adev)
{
	int i, j;
	u32 data;
	u32 active_rbs = 0;
	u32 rb_bitmap_width_per_sh = adev->gfx.config.max_backends_per_se /
					adev->gfx.config.max_sh_per_se;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v10_0_select_se_sh(adev, i, j, 0xffffffff);
			data = gfx_v10_0_get_rb_active_bitmap(adev);
			active_rbs |= data << ((i * adev->gfx.config.max_sh_per_se + j) *
					       rb_bitmap_width_per_sh);
		}
	}
	gfx_v10_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	adev->gfx.config.backend_enable_mask = active_rbs;
	adev->gfx.config.num_rbs = hweight32(active_rbs);
}

static u32 gfx_v10_0_init_pa_sc_tile_steering_override(struct amdgpu_device *adev)
{
	uint32_t num_sc;
	uint32_t enabled_rb_per_sh;
	uint32_t active_rb_bitmap;
	uint32_t num_rb_per_sc;
	uint32_t num_packer_per_sc;
	uint32_t pa_sc_tile_steering_override;

	/* init num_sc */
	num_sc = adev->gfx.config.max_shader_engines * adev->gfx.config.max_sh_per_se *
			adev->gfx.config.num_sc_per_sh;
	/* init num_rb_per_sc */
	active_rb_bitmap = gfx_v10_0_get_rb_active_bitmap(adev);
	enabled_rb_per_sh = hweight32(active_rb_bitmap);
	num_rb_per_sc = enabled_rb_per_sh / adev->gfx.config.num_sc_per_sh;
	/* init num_packer_per_sc */
	num_packer_per_sc = adev->gfx.config.num_packer_per_sc;

	pa_sc_tile_steering_override = 0;
	pa_sc_tile_steering_override |=
		(order_base_2(num_sc) << PA_SC_TILE_STEERING_OVERRIDE__NUM_SC__SHIFT) &
		PA_SC_TILE_STEERING_OVERRIDE__NUM_SC_MASK;
	pa_sc_tile_steering_override |=
		(order_base_2(num_rb_per_sc) << PA_SC_TILE_STEERING_OVERRIDE__NUM_RB_PER_SC__SHIFT) &
		PA_SC_TILE_STEERING_OVERRIDE__NUM_RB_PER_SC_MASK;
	pa_sc_tile_steering_override |=
		(order_base_2(num_packer_per_sc) << PA_SC_TILE_STEERING_OVERRIDE__NUM_PACKER_PER_SC__SHIFT) &
		PA_SC_TILE_STEERING_OVERRIDE__NUM_PACKER_PER_SC_MASK;

	return pa_sc_tile_steering_override;
}

#define DEFAULT_SH_MEM_BASES	(0x6000)
#define FIRST_COMPUTE_VMID	(8)
#define LAST_COMPUTE_VMID	(16)

static void gfx_v10_0_init_compute_vmid(struct amdgpu_device *adev)
{
	int i;
	uint32_t sh_mem_bases;

	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	sh_mem_bases = DEFAULT_SH_MEM_BASES | (DEFAULT_SH_MEM_BASES << 16);

	mutex_lock(&adev->srbm_mutex);
	for (i = FIRST_COMPUTE_VMID; i < LAST_COMPUTE_VMID; i++) {
		nv_grbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32_SOC15(GC, 0, mmSH_MEM_CONFIG, DEFAULT_SH_MEM_CONFIG);
		WREG32_SOC15(GC, 0, mmSH_MEM_BASES, sh_mem_bases);
	}
	nv_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	/* Initialize all compute VMIDs to have no GDS, GWS, or OA
	   acccess. These should be enabled by FW for target VMIDs. */
	for (i = FIRST_COMPUTE_VMID; i < LAST_COMPUTE_VMID; i++) {
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_BASE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_SIZE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_GWS_VMID0, i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_OA_VMID0, i, 0);
	}
}

static void gfx_v10_0_init_gds_vmid(struct amdgpu_device *adev)
{
	int vmid;

	/*
	 * Initialize all compute and user-gfx VMIDs to have no GDS, GWS, or OA
	 * access. Compute VMIDs should be enabled by FW for target VMIDs,
	 * the driver can enable them for graphics. VMID0 should maintain
	 * access so that HWS firmware can save/restore entries.
	 */
	for (vmid = 1; vmid < 16; vmid++) {
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_BASE, 2 * vmid, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_SIZE, 2 * vmid, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_GWS_VMID0, vmid, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_OA_VMID0, vmid, 0);
	}
}


static void gfx_v10_0_tcp_harvest(struct amdgpu_device *adev)
{
	int i, j, k;
	int max_wgp_per_sh = adev->gfx.config.max_cu_per_sh >> 1;
	u32 tmp, wgp_active_bitmap = 0;
	u32 gcrd_targets_disable_tcp = 0;
	u32 utcl_invreq_disable = 0;
	/*
	 * GCRD_TARGETS_DISABLE field contains
	 * for Navi10/Navi12: GL1C=[18:15], SQC=[14:10], TCP=[9:0]
	 * for Navi14: GL1C=[21:18], SQC=[17:12], TCP=[11:0]
	 */
	u32 gcrd_targets_disable_mask = amdgpu_gfx_create_bitmask(
		2 * max_wgp_per_sh + /* TCP */
		max_wgp_per_sh + /* SQC */
		4); /* GL1C */
	/*
	 * UTCL1_UTCL0_INVREQ_DISABLE field contains
	 * for Navi10Navi12: SQG=[24], RMI=[23:20], SQC=[19:10], TCP=[9:0]
	 * for Navi14: SQG=[28], RMI=[27:24], SQC=[23:12], TCP=[11:0]
	 */
	u32 utcl_invreq_disable_mask = amdgpu_gfx_create_bitmask(
		2 * max_wgp_per_sh + /* TCP */
		2 * max_wgp_per_sh + /* SQC */
		4 + /* RMI */
		1); /* SQG */

	if (adev->asic_type == CHIP_NAVI10 ||
	    adev->asic_type == CHIP_NAVI14 ||
	    adev->asic_type == CHIP_NAVI12) {
		mutex_lock(&adev->grbm_idx_mutex);
		for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
			for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
				gfx_v10_0_select_se_sh(adev, i, j, 0xffffffff);
				wgp_active_bitmap = gfx_v10_0_get_wgp_active_bitmap_per_sh(adev);
				/*
				 * Set corresponding TCP bits for the inactive WGPs in
				 * GCRD_SA_TARGETS_DISABLE
				 */
				gcrd_targets_disable_tcp = 0;
				/* Set TCP & SQC bits in UTCL1_UTCL0_INVREQ_DISABLE */
				utcl_invreq_disable = 0;

				for (k = 0; k < max_wgp_per_sh; k++) {
					if (!(wgp_active_bitmap & (1 << k))) {
						gcrd_targets_disable_tcp |= 3 << (2 * k);
						utcl_invreq_disable |= (3 << (2 * k)) |
							(3 << (2 * (max_wgp_per_sh + k)));
					}
				}

				tmp = RREG32_SOC15(GC, 0, mmUTCL1_UTCL0_INVREQ_DISABLE);
				/* only override TCP & SQC bits */
				tmp &= 0xffffffff << (4 * max_wgp_per_sh);
				tmp |= (utcl_invreq_disable & utcl_invreq_disable_mask);
				WREG32_SOC15(GC, 0, mmUTCL1_UTCL0_INVREQ_DISABLE, tmp);

				tmp = RREG32_SOC15(GC, 0, mmGCRD_SA_TARGETS_DISABLE);
				/* only override TCP bits */
				tmp &= 0xffffffff << (2 * max_wgp_per_sh);
				tmp |= (gcrd_targets_disable_tcp & gcrd_targets_disable_mask);
				WREG32_SOC15(GC, 0, mmGCRD_SA_TARGETS_DISABLE, tmp);
			}
		}

		gfx_v10_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
		mutex_unlock(&adev->grbm_idx_mutex);
	}
}

static void gfx_v10_0_get_tcc_info(struct amdgpu_device *adev)
{
	/* TCCs are global (not instanced). */
	uint32_t tcc_disable = RREG32_SOC15(GC, 0, mmCGTS_TCC_DISABLE) |
			       RREG32_SOC15(GC, 0, mmCGTS_USER_TCC_DISABLE);

	adev->gfx.config.tcc_disabled_mask =
		REG_GET_FIELD(tcc_disable, CGTS_TCC_DISABLE, TCC_DISABLE) |
		(REG_GET_FIELD(tcc_disable, CGTS_TCC_DISABLE, HI_TCC_DISABLE) << 16);
}

static void gfx_v10_0_constants_init(struct amdgpu_device *adev)
{
	u32 tmp;
	int i;

	WREG32_FIELD15(GC, 0, GRBM_CNTL, READ_TIMEOUT, 0xff);

	gfx_v10_0_tiling_mode_table_init(adev);

	gfx_v10_0_setup_rb(adev);
	gfx_v10_0_get_cu_info(adev, &adev->gfx.cu_info);
	gfx_v10_0_get_tcc_info(adev);
	adev->gfx.config.pa_sc_tile_steering_override =
		gfx_v10_0_init_pa_sc_tile_steering_override(adev);

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->vm_manager.id_mgr[AMDGPU_GFXHUB_0].num_ids; i++) {
		nv_grbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32_SOC15(GC, 0, mmSH_MEM_CONFIG, DEFAULT_SH_MEM_CONFIG);
		if (i != 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_BASES, PRIVATE_BASE,
				(adev->gmc.private_aperture_start >> 48));
			tmp = REG_SET_FIELD(tmp, SH_MEM_BASES, SHARED_BASE,
				(adev->gmc.shared_aperture_start >> 48));
			WREG32_SOC15(GC, 0, mmSH_MEM_BASES, tmp);
		}
	}
	nv_grbm_select(adev, 0, 0, 0, 0);

	mutex_unlock(&adev->srbm_mutex);

	gfx_v10_0_init_compute_vmid(adev);
	gfx_v10_0_init_gds_vmid(adev);

}

static void gfx_v10_0_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{
	u32 tmp = RREG32_SOC15(GC, 0, mmCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE,
			    enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE,
			    enable ? 1 : 0);

	WREG32_SOC15(GC, 0, mmCP_INT_CNTL_RING0, tmp);
}

static void gfx_v10_0_init_csb(struct amdgpu_device *adev)
{
	/* csib */
	WREG32_SOC15(GC, 0, mmRLC_CSIB_ADDR_HI,
		     adev->gfx.rlc.clear_state_gpu_addr >> 32);
	WREG32_SOC15(GC, 0, mmRLC_CSIB_ADDR_LO,
		     adev->gfx.rlc.clear_state_gpu_addr & 0xfffffffc);
	WREG32_SOC15(GC, 0, mmRLC_CSIB_LENGTH, adev->gfx.rlc.clear_state_size);
}

static void gfx_v10_0_init_pg(struct amdgpu_device *adev)
{
	int i;

	gfx_v10_0_init_csb(adev);

	for (i = 0; i < adev->num_vmhubs; i++)
		amdgpu_gmc_flush_gpu_tlb(adev, 0, i, 0);

	/* TODO: init power gating */
	return;
}

void gfx_v10_0_rlc_stop(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SOC15(GC, 0, mmRLC_CNTL);

	tmp = REG_SET_FIELD(tmp, RLC_CNTL, RLC_ENABLE_F32, 0);
	WREG32_SOC15(GC, 0, mmRLC_CNTL, tmp);
}

static void gfx_v10_0_rlc_reset(struct amdgpu_device *adev)
{
	WREG32_FIELD15(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	udelay(50);
	WREG32_FIELD15(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v10_0_rlc_smu_handshake_cntl(struct amdgpu_device *adev,
					     bool enable)
{
	uint32_t rlc_pg_cntl;

	rlc_pg_cntl = RREG32_SOC15(GC, 0, mmRLC_PG_CNTL);

	if (!enable) {
		/* RLC_PG_CNTL[23] = 0 (default)
		 * RLC will wait for handshake acks with SMU
		 * GFXOFF will be enabled
		 * RLC_PG_CNTL[23] = 1
		 * RLC will not issue any message to SMU
		 * hence no handshake between SMU & RLC
		 * GFXOFF will be disabled
		 */
		rlc_pg_cntl |= 0x800000;
	} else
		rlc_pg_cntl &= ~0x800000;
	WREG32_SOC15(GC, 0, mmRLC_PG_CNTL, rlc_pg_cntl);
}

static void gfx_v10_0_rlc_start(struct amdgpu_device *adev)
{
	/* TODO: enable rlc & smu handshake until smu
	 * and gfxoff feature works as expected */
	if (!(amdgpu_pp_feature_mask & PP_GFXOFF_MASK))
		gfx_v10_0_rlc_smu_handshake_cntl(adev, false);

	WREG32_FIELD15(GC, 0, RLC_CNTL, RLC_ENABLE_F32, 1);
	udelay(50);
}

static void gfx_v10_0_rlc_enable_srm(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* enable Save Restore Machine */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_CNTL));
	tmp |= RLC_SRM_CNTL__AUTO_INCR_ADDR_MASK;
	tmp |= RLC_SRM_CNTL__SRM_ENABLE_MASK;
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_CNTL), tmp);
}

static int gfx_v10_0_rlc_load_microcode(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			   le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

	WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_ADDR,
		     RLCG_UCODE_LOADING_START_ADDRESS);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_DATA,
			     le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_ADDR, adev->gfx.rlc_fw_version);

	return 0;
}

static int gfx_v10_0_rlc_resume(struct amdgpu_device *adev)
{
	int r;

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		r = gfx_v10_0_wait_for_rlc_autoload_complete(adev);
		if (r)
			return r;
		gfx_v10_0_init_pg(adev);

		/* enable RLC SRM */
		gfx_v10_0_rlc_enable_srm(adev);

	} else {
		adev->gfx.rlc.funcs->stop(adev);

		/* disable CG */
		WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, 0);

		/* disable PG */
		WREG32_SOC15(GC, 0, mmRLC_PG_CNTL, 0);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
			/* legacy rlc firmware loading */
			r = gfx_v10_0_rlc_load_microcode(adev);
			if (r)
				return r;
		} else if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
			/* rlc backdoor autoload firmware */
			r = gfx_v10_0_rlc_backdoor_autoload_enable(adev);
			if (r)
				return r;
		}

		gfx_v10_0_init_pg(adev);
		adev->gfx.rlc.funcs->start(adev);

		if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
			r = gfx_v10_0_wait_for_rlc_autoload_complete(adev);
			if (r)
				return r;
		}
	}
	return 0;
}

static struct {
	FIRMWARE_ID	id;
	unsigned int	offset;
	unsigned int	size;
} rlc_autoload_info[FIRMWARE_ID_MAX];

static int gfx_v10_0_parse_rlc_toc(struct amdgpu_device *adev)
{
	int ret;
	RLC_TABLE_OF_CONTENT *rlc_toc;

	ret = amdgpu_bo_create_reserved(adev, adev->psp.toc_bin_size, PAGE_SIZE,
					AMDGPU_GEM_DOMAIN_GTT,
					&adev->gfx.rlc.rlc_toc_bo,
					&adev->gfx.rlc.rlc_toc_gpu_addr,
					(void **)&adev->gfx.rlc.rlc_toc_buf);
	if (ret) {
		dev_err(adev->dev, "(%d) failed to create rlc toc bo\n", ret);
		return ret;
	}

	/* Copy toc from psp sos fw to rlc toc buffer */
	memcpy(adev->gfx.rlc.rlc_toc_buf, adev->psp.toc_start_addr, adev->psp.toc_bin_size);

	rlc_toc = (RLC_TABLE_OF_CONTENT *)adev->gfx.rlc.rlc_toc_buf;
	while (rlc_toc && (rlc_toc->id > FIRMWARE_ID_INVALID) &&
		(rlc_toc->id < FIRMWARE_ID_MAX)) {
		if ((rlc_toc->id >= FIRMWARE_ID_CP_CE) &&
		    (rlc_toc->id <= FIRMWARE_ID_CP_MES)) {
			/* Offset needs 4KB alignment */
			rlc_toc->offset = ALIGN(rlc_toc->offset * 4, PAGE_SIZE);
		}

		rlc_autoload_info[rlc_toc->id].id = rlc_toc->id;
		rlc_autoload_info[rlc_toc->id].offset = rlc_toc->offset * 4;
		rlc_autoload_info[rlc_toc->id].size = rlc_toc->size * 4;

		rlc_toc++;
	};

	return 0;
}

static uint32_t gfx_v10_0_calc_toc_total_size(struct amdgpu_device *adev)
{
	uint32_t total_size = 0;
	FIRMWARE_ID id;
	int ret;

	ret = gfx_v10_0_parse_rlc_toc(adev);
	if (ret) {
		dev_err(adev->dev, "failed to parse rlc toc\n");
		return 0;
	}

	for (id = FIRMWARE_ID_RLC_G_UCODE; id < FIRMWARE_ID_MAX; id++)
		total_size += rlc_autoload_info[id].size;

	/* In case the offset in rlc toc ucode is aligned */
	if (total_size < rlc_autoload_info[FIRMWARE_ID_MAX-1].offset)
		total_size = rlc_autoload_info[FIRMWARE_ID_MAX-1].offset +
				rlc_autoload_info[FIRMWARE_ID_MAX-1].size;

	return total_size;
}

static int gfx_v10_0_rlc_backdoor_autoload_buffer_init(struct amdgpu_device *adev)
{
	int r;
	uint32_t total_size;

	total_size = gfx_v10_0_calc_toc_total_size(adev);

	r = amdgpu_bo_create_reserved(adev, total_size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.rlc.rlc_autoload_bo,
				      &adev->gfx.rlc.rlc_autoload_gpu_addr,
				      (void **)&adev->gfx.rlc.rlc_autoload_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create fw autoload bo\n", r);
		return r;
	}

	return 0;
}

static void gfx_v10_0_rlc_backdoor_autoload_buffer_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.rlc.rlc_toc_bo,
			      &adev->gfx.rlc.rlc_toc_gpu_addr,
			      (void **)&adev->gfx.rlc.rlc_toc_buf);
	amdgpu_bo_free_kernel(&adev->gfx.rlc.rlc_autoload_bo,
			      &adev->gfx.rlc.rlc_autoload_gpu_addr,
			      (void **)&adev->gfx.rlc.rlc_autoload_ptr);
}

static void gfx_v10_0_rlc_backdoor_autoload_copy_ucode(struct amdgpu_device *adev,
						       FIRMWARE_ID id,
						       const void *fw_data,
						       uint32_t fw_size)
{
	uint32_t toc_offset;
	uint32_t toc_fw_size;
	char *ptr = adev->gfx.rlc.rlc_autoload_ptr;

	if (id <= FIRMWARE_ID_INVALID || id >= FIRMWARE_ID_MAX)
		return;

	toc_offset = rlc_autoload_info[id].offset;
	toc_fw_size = rlc_autoload_info[id].size;

	if (fw_size == 0)
		fw_size = toc_fw_size;

	if (fw_size > toc_fw_size)
		fw_size = toc_fw_size;

	memcpy(ptr + toc_offset, fw_data, fw_size);

	if (fw_size < toc_fw_size)
		memset(ptr + toc_offset + fw_size, 0, toc_fw_size - fw_size);
}

static void gfx_v10_0_rlc_backdoor_autoload_copy_toc_ucode(struct amdgpu_device *adev)
{
	void *data;
	uint32_t size;

	data = adev->gfx.rlc.rlc_toc_buf;
	size = rlc_autoload_info[FIRMWARE_ID_RLC_TOC].size;

	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_RLC_TOC,
						   data, size);
}

static void gfx_v10_0_rlc_backdoor_autoload_copy_gfx_ucode(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	uint32_t fw_size;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;

	/* pfp ucode */
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.pfp_fw->data;
	fw_data = (const __le32 *)(adev->gfx.pfp_fw->data +
		le32_to_cpu(cp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_CP_PFP,
						   fw_data, fw_size);

	/* ce ucode */
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.ce_fw->data;
	fw_data = (const __le32 *)(adev->gfx.ce_fw->data +
		le32_to_cpu(cp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_CP_CE,
						   fw_data, fw_size);

	/* me ucode */
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.me_fw->data;
	fw_data = (const __le32 *)(adev->gfx.me_fw->data +
		le32_to_cpu(cp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes);
	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_CP_ME,
						   fw_data, fw_size);

	/* rlc ucode */
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)
		adev->gfx.rlc_fw->data;
	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
		le32_to_cpu(rlc_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(rlc_hdr->header.ucode_size_bytes);
	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_RLC_G_UCODE,
						   fw_data, fw_size);

	/* mec1 ucode */
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.mec_fw->data;
	fw_data = (const __le32 *) (adev->gfx.mec_fw->data +
		le32_to_cpu(cp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(cp_hdr->header.ucode_size_bytes) -
		cp_hdr->jt_size * 4;
	gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
						   FIRMWARE_ID_CP_MEC,
						   fw_data, fw_size);
	/* mec2 ucode is not necessary if mec2 ucode is same as mec1 */
}

/* Temporarily put sdma part here */
static void gfx_v10_0_rlc_backdoor_autoload_copy_sdma_ucode(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	uint32_t fw_size;
	const struct sdma_firmware_header_v1_0 *sdma_hdr;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		sdma_hdr = (const struct sdma_firmware_header_v1_0 *)
			adev->sdma.instance[i].fw->data;
		fw_data = (const __le32 *) (adev->sdma.instance[i].fw->data +
			le32_to_cpu(sdma_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(sdma_hdr->header.ucode_size_bytes);

		if (i == 0) {
			gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
				FIRMWARE_ID_SDMA0_UCODE, fw_data, fw_size);
			gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
				FIRMWARE_ID_SDMA0_JT,
				(uint32_t *)fw_data +
				sdma_hdr->jt_offset,
				sdma_hdr->jt_size * 4);
		} else if (i == 1) {
			gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
				FIRMWARE_ID_SDMA1_UCODE, fw_data, fw_size);
			gfx_v10_0_rlc_backdoor_autoload_copy_ucode(adev,
				FIRMWARE_ID_SDMA1_JT,
				(uint32_t *)fw_data +
				sdma_hdr->jt_offset,
				sdma_hdr->jt_size * 4);
		}
	}
}

static int gfx_v10_0_rlc_backdoor_autoload_enable(struct amdgpu_device *adev)
{
	uint32_t rlc_g_offset, rlc_g_size, tmp;
	uint64_t gpu_addr;

	gfx_v10_0_rlc_backdoor_autoload_copy_toc_ucode(adev);
	gfx_v10_0_rlc_backdoor_autoload_copy_sdma_ucode(adev);
	gfx_v10_0_rlc_backdoor_autoload_copy_gfx_ucode(adev);

	rlc_g_offset = rlc_autoload_info[FIRMWARE_ID_RLC_G_UCODE].offset;
	rlc_g_size = rlc_autoload_info[FIRMWARE_ID_RLC_G_UCODE].size;
	gpu_addr = adev->gfx.rlc.rlc_autoload_gpu_addr + rlc_g_offset;

	WREG32_SOC15(GC, 0, mmRLC_HYP_BOOTLOAD_ADDR_HI, upper_32_bits(gpu_addr));
	WREG32_SOC15(GC, 0, mmRLC_HYP_BOOTLOAD_ADDR_LO, lower_32_bits(gpu_addr));
	WREG32_SOC15(GC, 0, mmRLC_HYP_BOOTLOAD_SIZE, rlc_g_size);

	tmp = RREG32_SOC15(GC, 0, mmRLC_HYP_RESET_VECTOR);
	if (!(tmp & (RLC_HYP_RESET_VECTOR__COLD_BOOT_EXIT_MASK |
		   RLC_HYP_RESET_VECTOR__VDDGFX_EXIT_MASK))) {
		DRM_ERROR("Neither COLD_BOOT_EXIT nor VDDGFX_EXIT is set\n");
		return -EINVAL;
	}

	tmp = RREG32_SOC15(GC, 0, mmRLC_CNTL);
	if (tmp & RLC_CNTL__RLC_ENABLE_F32_MASK) {
		DRM_ERROR("RLC ROM should halt itself\n");
		return -EINVAL;
	}

	return 0;
}

static int gfx_v10_0_rlc_backdoor_autoload_config_me_cache(struct amdgpu_device *adev)
{
	uint32_t usec_timeout = 50000;  /* wait for 50ms */
	uint32_t tmp;
	int i;
	uint64_t addr;

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_ME_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	/* Program me ucode address into intruction cache address register */
	addr = adev->gfx.rlc.rlc_autoload_gpu_addr +
		rlc_autoload_info[FIRMWARE_ID_CP_ME].offset;
	WREG32_SOC15(GC, 0, mmCP_ME_IC_BASE_LO,
			lower_32_bits(addr) & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_ME_IC_BASE_HI,
			upper_32_bits(addr));

	return 0;
}

static int gfx_v10_0_rlc_backdoor_autoload_config_ce_cache(struct amdgpu_device *adev)
{
	uint32_t usec_timeout = 50000;  /* wait for 50ms */
	uint32_t tmp;
	int i;
	uint64_t addr;

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_CE_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	/* Program ce ucode address into intruction cache address register */
	addr = adev->gfx.rlc.rlc_autoload_gpu_addr +
		rlc_autoload_info[FIRMWARE_ID_CP_CE].offset;
	WREG32_SOC15(GC, 0, mmCP_CE_IC_BASE_LO,
			lower_32_bits(addr) & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_CE_IC_BASE_HI,
			upper_32_bits(addr));

	return 0;
}

static int gfx_v10_0_rlc_backdoor_autoload_config_pfp_cache(struct amdgpu_device *adev)
{
	uint32_t usec_timeout = 50000;  /* wait for 50ms */
	uint32_t tmp;
	int i;
	uint64_t addr;

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_PFP_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	/* Program pfp ucode address into intruction cache address register */
	addr = adev->gfx.rlc.rlc_autoload_gpu_addr +
		rlc_autoload_info[FIRMWARE_ID_CP_PFP].offset;
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_LO,
			lower_32_bits(addr) & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_HI,
			upper_32_bits(addr));

	return 0;
}

static int gfx_v10_0_rlc_backdoor_autoload_config_mec_cache(struct amdgpu_device *adev)
{
	uint32_t usec_timeout = 50000;  /* wait for 50ms */
	uint32_t tmp;
	int i;
	uint64_t addr;

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_CPC_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	/* Program mec1 ucode address into intruction cache address register */
	addr = adev->gfx.rlc.rlc_autoload_gpu_addr +
		rlc_autoload_info[FIRMWARE_ID_CP_MEC].offset;
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_LO,
			lower_32_bits(addr) & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_HI,
			upper_32_bits(addr));

	return 0;
}

static int gfx_v10_0_wait_for_rlc_autoload_complete(struct amdgpu_device *adev)
{
	uint32_t cp_status;
	uint32_t bootload_status;
	int i, r;

	for (i = 0; i < adev->usec_timeout; i++) {
		cp_status = RREG32_SOC15(GC, 0, mmCP_STAT);
		bootload_status = RREG32_SOC15(GC, 0, mmRLC_RLCS_BOOTLOAD_STATUS);
		if ((cp_status == 0) &&
		    (REG_GET_FIELD(bootload_status,
			RLC_RLCS_BOOTLOAD_STATUS, BOOTLOAD_COMPLETE) == 1)) {
			break;
		}
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		dev_err(adev->dev, "rlc autoload: gc ucode autoload timeout\n");
		return -ETIMEDOUT;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO) {
		r = gfx_v10_0_rlc_backdoor_autoload_config_me_cache(adev);
		if (r)
			return r;

		r = gfx_v10_0_rlc_backdoor_autoload_config_ce_cache(adev);
		if (r)
			return r;

		r = gfx_v10_0_rlc_backdoor_autoload_config_pfp_cache(adev);
		if (r)
			return r;

		r = gfx_v10_0_rlc_backdoor_autoload_config_mec_cache(adev);
		if (r)
			return r;
	}

	return 0;
}

static void gfx_v10_0_cp_gfx_enable(struct amdgpu_device *adev, bool enable)
{
	int i;
	u32 tmp = RREG32_SOC15(GC, 0, mmCP_ME_CNTL);

	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, enable ? 0 : 1);
	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, enable ? 0 : 1);
	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, enable ? 0 : 1);
	if (!enable) {
		for (i = 0; i < adev->gfx.num_gfx_rings; i++)
			adev->gfx.gfx_ring[i].sched.ready = false;
	}
	WREG32_SOC15(GC, 0, mmCP_ME_CNTL, tmp);
	udelay(50);
}

static int gfx_v10_0_cp_gfx_load_pfp_microcode(struct amdgpu_device *adev)
{
	int r;
	const struct gfx_firmware_header_v1_0 *pfp_hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;
	uint32_t tmp;
	uint32_t usec_timeout = 50000;  /* wait for 50ms */

	pfp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.pfp_fw->data;

	amdgpu_ucode_print_gfx_hdr(&pfp_hdr->header);

	fw_data = (const __le32 *)(adev->gfx.pfp_fw->data +
		le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, pfp_hdr->header.ucode_size_bytes,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.pfp.pfp_fw_obj,
				      &adev->gfx.pfp.pfp_fw_gpu_addr,
				      (void **)&adev->gfx.pfp.pfp_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create pfp fw bo\n", r);
		gfx_v10_0_pfp_fini(adev);
		return r;
	}

	memcpy(adev->gfx.pfp.pfp_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->gfx.pfp.pfp_fw_obj);
	amdgpu_bo_unreserve(adev->gfx.pfp.pfp_fw_obj);

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_PFP_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_PFP_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	if (amdgpu_emu_mode == 1)
		adev->nbio_funcs->hdp_flush(adev, NULL);

	tmp = RREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_BASE_CNTL, EXE_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, CP_PFP_IC_BASE_CNTL, ADDRESS_CLAMP, 1);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_CNTL, tmp);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_LO,
		adev->gfx.pfp.pfp_fw_gpu_addr & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_PFP_IC_BASE_HI,
		upper_32_bits(adev->gfx.pfp.pfp_fw_gpu_addr));

	return 0;
}

static int gfx_v10_0_cp_gfx_load_ce_microcode(struct amdgpu_device *adev)
{
	int r;
	const struct gfx_firmware_header_v1_0 *ce_hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;
	uint32_t tmp;
	uint32_t usec_timeout = 50000;  /* wait for 50ms */

	ce_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.ce_fw->data;

	amdgpu_ucode_print_gfx_hdr(&ce_hdr->header);

	fw_data = (const __le32 *)(adev->gfx.ce_fw->data +
		le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, ce_hdr->header.ucode_size_bytes,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.ce.ce_fw_obj,
				      &adev->gfx.ce.ce_fw_gpu_addr,
				      (void **)&adev->gfx.ce.ce_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create ce fw bo\n", r);
		gfx_v10_0_ce_fini(adev);
		return r;
	}

	memcpy(adev->gfx.ce.ce_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->gfx.ce.ce_fw_obj);
	amdgpu_bo_unreserve(adev->gfx.ce.ce_fw_obj);

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_CE_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_CE_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	if (amdgpu_emu_mode == 1)
		adev->nbio_funcs->hdp_flush(adev, NULL);

	tmp = RREG32_SOC15(GC, 0, mmCP_CE_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_BASE_CNTL, EXE_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, CP_CE_IC_BASE_CNTL, ADDRESS_CLAMP, 1);
	WREG32_SOC15(GC, 0, mmCP_CE_IC_BASE_LO,
		adev->gfx.ce.ce_fw_gpu_addr & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_CE_IC_BASE_HI,
		upper_32_bits(adev->gfx.ce.ce_fw_gpu_addr));

	return 0;
}

static int gfx_v10_0_cp_gfx_load_me_microcode(struct amdgpu_device *adev)
{
	int r;
	const struct gfx_firmware_header_v1_0 *me_hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;
	uint32_t tmp;
	uint32_t usec_timeout = 50000;  /* wait for 50ms */

	me_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.me_fw->data;

	amdgpu_ucode_print_gfx_hdr(&me_hdr->header);

	fw_data = (const __le32 *)(adev->gfx.me_fw->data +
		le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, me_hdr->header.ucode_size_bytes,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.me.me_fw_obj,
				      &adev->gfx.me.me_fw_gpu_addr,
				      (void **)&adev->gfx.me.me_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create me fw bo\n", r);
		gfx_v10_0_me_fini(adev);
		return r;
	}

	memcpy(adev->gfx.me.me_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->gfx.me.me_fw_obj);
	amdgpu_bo_unreserve(adev->gfx.me.me_fw_obj);

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_ME_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_ME_IC_OP_CNTL,
			INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	if (amdgpu_emu_mode == 1)
		adev->nbio_funcs->hdp_flush(adev, NULL);

	tmp = RREG32_SOC15(GC, 0, mmCP_ME_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_BASE_CNTL, EXE_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, CP_ME_IC_BASE_CNTL, ADDRESS_CLAMP, 1);
	WREG32_SOC15(GC, 0, mmCP_ME_IC_BASE_LO,
		adev->gfx.me.me_fw_gpu_addr & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_ME_IC_BASE_HI,
		upper_32_bits(adev->gfx.me.me_fw_gpu_addr));

	return 0;
}

static int gfx_v10_0_cp_gfx_load_microcode(struct amdgpu_device *adev)
{
	int r;

	if (!adev->gfx.me_fw || !adev->gfx.pfp_fw || !adev->gfx.ce_fw)
		return -EINVAL;

	gfx_v10_0_cp_gfx_enable(adev, false);

	r = gfx_v10_0_cp_gfx_load_pfp_microcode(adev);
	if (r) {
		dev_err(adev->dev, "(%d) failed to load pfp fw\n", r);
		return r;
	}

	r = gfx_v10_0_cp_gfx_load_ce_microcode(adev);
	if (r) {
		dev_err(adev->dev, "(%d) failed to load ce fw\n", r);
		return r;
	}

	r = gfx_v10_0_cp_gfx_load_me_microcode(adev);
	if (r) {
		dev_err(adev->dev, "(%d) failed to load me fw\n", r);
		return r;
	}

	return 0;
}

static int gfx_v10_0_cp_gfx_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	int r, i;
	int ctx_reg_offset;

	/* init the CP */
	WREG32_SOC15(GC, 0, mmCP_MAX_CONTEXT,
		     adev->gfx.config.max_hw_contexts - 1);
	WREG32_SOC15(GC, 0, mmCP_DEVICE_ID, 1);

	gfx_v10_0_cp_gfx_enable(adev, true);

	ring = &adev->gfx.gfx_ring[0];
	r = amdgpu_ring_alloc(ring, gfx_v10_0_get_csb_size(adev) + 4);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, 0x80000000);
	amdgpu_ring_write(ring, 0x80000000);

	for (sect = gfx10_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				amdgpu_ring_write(ring,
						  PACKET3(PACKET3_SET_CONTEXT_REG,
							  ext->reg_count));
				amdgpu_ring_write(ring, ext->reg_index -
						  PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					amdgpu_ring_write(ring, ext->extent[i]);
			}
		}
	}

	ctx_reg_offset =
		SOC15_REG_OFFSET(GC, 0, mmPA_SC_TILE_STEERING_OVERRIDE) - PACKET3_SET_CONTEXT_REG_START;
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	amdgpu_ring_write(ring, ctx_reg_offset);
	amdgpu_ring_write(ring, adev->gfx.config.pa_sc_tile_steering_override);

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	amdgpu_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	amdgpu_ring_write(ring, 0x8000);
	amdgpu_ring_write(ring, 0x8000);

	amdgpu_ring_commit(ring);

	/* submit cs packet to copy state 0 to next available state */
	ring = &adev->gfx.gfx_ring[1];
	r = amdgpu_ring_alloc(ring, 2);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_commit(ring);

	return 0;
}

static void gfx_v10_0_cp_gfx_switch_pipe(struct amdgpu_device *adev,
					 CP_PIPE_ID pipe)
{
	u32 tmp;

	tmp = RREG32_SOC15(GC, 0, mmGRBM_GFX_CNTL);
	tmp = REG_SET_FIELD(tmp, GRBM_GFX_CNTL, PIPEID, pipe);

	WREG32_SOC15(GC, 0, mmGRBM_GFX_CNTL, tmp);
}

static void gfx_v10_0_cp_gfx_set_doorbell(struct amdgpu_device *adev,
					  struct amdgpu_ring *ring)
{
	u32 tmp;

	tmp = RREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL);
	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_EN, 0);
	}
	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL, tmp);
	tmp = REG_SET_FIELD(0, CP_RB_DOORBELL_RANGE_LOWER,
			    DOORBELL_RANGE_LOWER, ring->doorbell_index);
	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_RANGE_LOWER, tmp);

	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_RANGE_UPPER,
		     CP_RB_DOORBELL_RANGE_UPPER__DOORBELL_RANGE_UPPER_MASK);
}

static int gfx_v10_0_cp_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr, rptr_addr, wptr_gpu_addr;
	u32 i;

	/* Set the write pointer delay */
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32_SOC15(GC, 0, mmCP_RB_VMID, 0);

	/* Init gfx ring 0 for pipe 0 */
	mutex_lock(&adev->srbm_mutex);
	gfx_v10_0_cp_gfx_switch_pipe(adev, PIPE_ID0);
	mutex_unlock(&adev->srbm_mutex);
	/* Set ring buffer size */
	ring = &adev->gfx.gfx_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = REG_SET_FIELD(0, CP_RB0_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, RB_BLKSZ, rb_bufsz - 2);
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, BUF_SWAP, 1);
#endif
	WREG32_SOC15(GC, 0, mmCP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's write pointers */
	ring->wptr = 0;
	WREG32_SOC15(GC, 0, mmCP_RB0_WPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI, upper_32_bits(ring->wptr));

	/* set the wb address wether it's enabled or not */
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB0_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32_SOC15(GC, 0, mmCP_RB0_RPTR_ADDR_HI, upper_32_bits(rptr_addr) &
		     CP_RB_RPTR_ADDR_HI__RB_RPTR_ADDR_HI_MASK);

	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_LO,
		     lower_32_bits(wptr_gpu_addr));
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_HI,
		     upper_32_bits(wptr_gpu_addr));

	mdelay(1);
	WREG32_SOC15(GC, 0, mmCP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32_SOC15(GC, 0, mmCP_RB0_BASE, rb_addr);
	WREG32_SOC15(GC, 0, mmCP_RB0_BASE_HI, upper_32_bits(rb_addr));

	WREG32_SOC15(GC, 0, mmCP_RB_ACTIVE, 1);

	gfx_v10_0_cp_gfx_set_doorbell(adev, ring);

	/* Init gfx ring 1 for pipe 1 */
	mutex_lock(&adev->srbm_mutex);
	gfx_v10_0_cp_gfx_switch_pipe(adev, PIPE_ID1);
	mutex_unlock(&adev->srbm_mutex);
	ring = &adev->gfx.gfx_ring[1];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = REG_SET_FIELD(0, CP_RB1_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_RB1_CNTL, RB_BLKSZ, rb_bufsz - 2);
	WREG32_SOC15(GC, 0, mmCP_RB1_CNTL, tmp);
	/* Initialize the ring buffer's write pointers */
	ring->wptr = 0;
	WREG32_SOC15(GC, 0, mmCP_RB1_WPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(GC, 0, mmCP_RB1_WPTR_HI, upper_32_bits(ring->wptr));
	/* Set the wb address wether it's enabled or not */
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB1_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32_SOC15(GC, 0, mmCP_RB1_RPTR_ADDR_HI, upper_32_bits(rptr_addr) &
		CP_RB1_RPTR_ADDR_HI__RB_RPTR_ADDR_HI_MASK);
	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_LO,
		lower_32_bits(wptr_gpu_addr));
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_HI,
		upper_32_bits(wptr_gpu_addr));

	mdelay(1);
	WREG32_SOC15(GC, 0, mmCP_RB1_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32_SOC15(GC, 0, mmCP_RB1_BASE, rb_addr);
	WREG32_SOC15(GC, 0, mmCP_RB1_BASE_HI, upper_32_bits(rb_addr));
	WREG32_SOC15(GC, 0, mmCP_RB1_ACTIVE, 1);

	gfx_v10_0_cp_gfx_set_doorbell(adev, ring);

	/* Switch to pipe 0 */
	mutex_lock(&adev->srbm_mutex);
	gfx_v10_0_cp_gfx_switch_pipe(adev, PIPE_ID0);
	mutex_unlock(&adev->srbm_mutex);

	/* start the ring */
	gfx_v10_0_cp_gfx_start(adev);

	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->sched.ready = true;
	}

	return 0;
}

static void gfx_v10_0_cp_compute_enable(struct amdgpu_device *adev, bool enable)
{
	int i;

	if (enable) {
		WREG32_SOC15(GC, 0, mmCP_MEC_CNTL, 0);
	} else {
		WREG32_SOC15(GC, 0, mmCP_MEC_CNTL,
			     (CP_MEC_CNTL__MEC_ME1_HALT_MASK |
			      CP_MEC_CNTL__MEC_ME2_HALT_MASK));
		for (i = 0; i < adev->gfx.num_compute_rings; i++)
			adev->gfx.compute_ring[i].sched.ready = false;
		adev->gfx.kiq.ring.sched.ready = false;
	}
	udelay(50);
}

static int gfx_v10_0_cp_compute_load_microcode(struct amdgpu_device *adev)
{
	const struct gfx_firmware_header_v1_0 *mec_hdr;
	const __le32 *fw_data;
	unsigned i;
	u32 tmp;
	u32 usec_timeout = 50000; /* Wait for 50 ms */

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	gfx_v10_0_cp_compute_enable(adev, false);

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	amdgpu_ucode_print_gfx_hdr(&mec_hdr->header);

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));

	/* Trigger an invalidation of the L1 instruction caches */
	tmp = RREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_OP_CNTL, INVALIDATE_CACHE, 1);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL, tmp);

	/* Wait for invalidation complete */
	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32_SOC15(GC, 0, mmCP_CPC_IC_OP_CNTL);
		if (1 == REG_GET_FIELD(tmp, CP_CPC_IC_OP_CNTL,
				       INVALIDATE_CACHE_COMPLETE))
			break;
		udelay(1);
	}

	if (i >= usec_timeout) {
		dev_err(adev->dev, "failed to invalidate instruction cache\n");
		return -EINVAL;
	}

	if (amdgpu_emu_mode == 1)
		adev->nbio_funcs->hdp_flush(adev, NULL);

	tmp = RREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, EXE_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, ADDRESS_CLAMP, 1);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_CNTL, tmp);

	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_LO, adev->gfx.mec.mec_fw_gpu_addr &
		     0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_HI,
		     upper_32_bits(adev->gfx.mec.mec_fw_gpu_addr));

	/* MEC1 */
	WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_ADDR, 0);

	for (i = 0; i < mec_hdr->jt_size; i++)
		WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_DATA,
			     le32_to_cpup(fw_data + mec_hdr->jt_offset + i));

	WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_ADDR, adev->gfx.mec_fw_version);

	/*
	 * TODO: Loading MEC2 firmware is only necessary if MEC2 should run
	 * different microcode than MEC1.
	 */

	return 0;
}

static void gfx_v10_0_kiq_setting(struct amdgpu_ring *ring)
{
	uint32_t tmp;
	struct amdgpu_device *adev = ring->adev;

	/* tell RLC which is KIQ queue */
	tmp = RREG32_SOC15(GC, 0, mmRLC_CP_SCHEDULERS);
	tmp &= 0xffffff00;
	tmp |= (ring->me << 5) | (ring->pipe << 3) | (ring->queue);
	WREG32_SOC15(GC, 0, mmRLC_CP_SCHEDULERS, tmp);
	tmp |= 0x80;
	WREG32_SOC15(GC, 0, mmRLC_CP_SCHEDULERS, tmp);
}

static int gfx_v10_0_gfx_mqd_init(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_gfx_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr;
	uint32_t tmp;
	uint32_t rb_bufsz;

	/* set up gfx hqd wptr */
	mqd->cp_gfx_hqd_wptr = 0;
	mqd->cp_gfx_hqd_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set up mqd control */
	tmp = RREG32_SOC15(GC, 0, mmCP_GFX_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_GFX_MQD_CONTROL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_GFX_MQD_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_GFX_MQD_CONTROL, CACHE_POLICY, 0);
	mqd->cp_gfx_mqd_control = tmp;

	/* set up gfx_hqd_vimd with 0x0 to indicate the ring buffer's vmid */
	tmp = RREG32_SOC15(GC, 0, mmCP_GFX_HQD_VMID);
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_VMID, VMID, 0);
	mqd->cp_gfx_hqd_vmid = 0;

	/* set up default queue priority level
	 * 0x0 = low priority, 0x1 = high priority */
	tmp = RREG32_SOC15(GC, 0, mmCP_GFX_HQD_QUEUE_PRIORITY);
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_QUEUE_PRIORITY, PRIORITY_LEVEL, 0);
	mqd->cp_gfx_hqd_queue_priority = tmp;

	/* set up time quantum */
	tmp = RREG32_SOC15(GC, 0, mmCP_GFX_HQD_QUANTUM);
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_QUANTUM, QUANTUM_EN, 1);
	mqd->cp_gfx_hqd_quantum = tmp;

	/* set up gfx hqd base. this is similar as CP_RB_BASE */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_gfx_hqd_base = hqd_gpu_addr;
	mqd->cp_gfx_hqd_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up hqd_rptr_addr/_hi, similar as CP_RB_RPTR */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	mqd->cp_gfx_hqd_rptr_addr = wb_gpu_addr & 0xfffffffc;
	mqd->cp_gfx_hqd_rptr_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* set up rb_wptr_poll addr */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	mqd->cp_rb_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_rb_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	/* set up the gfx_hqd_control, similar as CP_RB0_CNTL */
	rb_bufsz = order_base_2(ring->ring_size / 4) - 1;
	tmp = RREG32_SOC15(GC, 0, mmCP_GFX_HQD_CNTL);
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_CNTL, RB_BLKSZ, rb_bufsz - 2);
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_GFX_HQD_CNTL, BUF_SWAP, 1);
#endif
	mqd->cp_gfx_hqd_cntl = tmp;

	/* set up cp_doorbell_control */
	tmp = RREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL);
	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
	} else
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_EN, 0);
	mqd->cp_rb_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_gfx_hqd_rptr = RREG32_SOC15(GC, 0, mmCP_GFX_HQD_RPTR);

	/* active the queue */
	mqd->cp_gfx_hqd_active = 1;

	return 0;
}

#ifdef BRING_UP_DEBUG
static int gfx_v10_0_gfx_queue_init_register(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_gfx_mqd *mqd = ring->mqd_ptr;

	/* set mmCP_GFX_HQD_WPTR/_HI to 0 */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_WPTR, mqd->cp_gfx_hqd_wptr);
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_WPTR_HI, mqd->cp_gfx_hqd_wptr_hi);

	/* set GFX_MQD_BASE */
	WREG32_SOC15(GC, 0, mmCP_MQD_BASE_ADDR, mqd->cp_mqd_base_addr);
	WREG32_SOC15(GC, 0, mmCP_MQD_BASE_ADDR_HI, mqd->cp_mqd_base_addr_hi);

	/* set GFX_MQD_CONTROL */
	WREG32_SOC15(GC, 0, mmCP_GFX_MQD_CONTROL, mqd->cp_gfx_mqd_control);

	/* set GFX_HQD_VMID to 0 */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_VMID, mqd->cp_gfx_hqd_vmid);

	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_QUEUE_PRIORITY,
			mqd->cp_gfx_hqd_queue_priority);
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_QUANTUM, mqd->cp_gfx_hqd_quantum);

	/* set GFX_HQD_BASE, similar as CP_RB_BASE */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_BASE, mqd->cp_gfx_hqd_base);
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_BASE_HI, mqd->cp_gfx_hqd_base_hi);

	/* set GFX_HQD_RPTR_ADDR, similar as CP_RB_RPTR */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_RPTR_ADDR, mqd->cp_gfx_hqd_rptr_addr);
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_RPTR_ADDR_HI, mqd->cp_gfx_hqd_rptr_addr_hi);

	/* set GFX_HQD_CNTL, similar as CP_RB_CNTL */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_CNTL, mqd->cp_gfx_hqd_cntl);

	/* set RB_WPTR_POLL_ADDR */
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_LO, mqd->cp_rb_wptr_poll_addr_lo);
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_HI, mqd->cp_rb_wptr_poll_addr_hi);

	/* set RB_DOORBELL_CONTROL */
	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL, mqd->cp_rb_doorbell_control);

	/* active the queue */
	WREG32_SOC15(GC, 0, mmCP_GFX_HQD_ACTIVE, mqd->cp_gfx_hqd_active);

	return 0;
}
#endif

static int gfx_v10_0_gfx_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_gfx_mqd *mqd = ring->mqd_ptr;

	if (!adev->in_gpu_reset && !adev->in_suspend) {
		memset((void *)mqd, 0, sizeof(*mqd));
		mutex_lock(&adev->srbm_mutex);
		nv_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v10_0_gfx_mqd_init(ring);
#ifdef BRING_UP_DEBUG
		gfx_v10_0_gfx_queue_init_register(ring);
#endif
		nv_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
		if (adev->gfx.me.mqd_backup[AMDGPU_MAX_GFX_RINGS])
			memcpy(adev->gfx.me.mqd_backup[AMDGPU_MAX_GFX_RINGS], mqd, sizeof(*mqd));
	} else if (adev->in_gpu_reset) {
		/* reset mqd with the backup copy */
		if (adev->gfx.me.mqd_backup[AMDGPU_MAX_GFX_RINGS])
			memcpy(mqd, adev->gfx.me.mqd_backup[AMDGPU_MAX_GFX_RINGS], sizeof(*mqd));
		/* reset the ring */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);
#ifdef BRING_UP_DEBUG
		mutex_lock(&adev->srbm_mutex);
		nv_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v10_0_gfx_queue_init_register(ring);
		nv_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
#endif
	} else {
		amdgpu_ring_clear_ring(ring);
	}

	return 0;
}

#ifndef BRING_UP_DEBUG
static int gfx_v10_0_kiq_enable_kgq(struct amdgpu_device *adev)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq.ring;
	int r, i;

	if (!kiq->pmf || !kiq->pmf->kiq_map_queues)
		return -EINVAL;

	r = amdgpu_ring_alloc(kiq_ring, kiq->pmf->map_queues_size *
					adev->gfx.num_gfx_rings);
	if (r) {
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);
		return r;
	}

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		kiq->pmf->kiq_map_queues(kiq_ring, &adev->gfx.gfx_ring[i]);

	r = amdgpu_ring_test_ring(kiq_ring);
	if (r) {
		DRM_ERROR("kfq enable failed\n");
		kiq_ring->sched.ready = false;
	}
	return r;
}
#endif

static int gfx_v10_0_cp_async_gfx_ring_resume(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *ring;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;

		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
		if (!r) {
			r = gfx_v10_0_gfx_init_queue(ring);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}
#ifndef BRING_UP_DEBUG
	r = gfx_v10_0_kiq_enable_kgq(adev);
	if (r)
		goto done;
#endif
	r = gfx_v10_0_cp_gfx_start(adev);
	if (r)
		goto done;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->sched.ready = true;
	}
done:
	return r;
}

static int gfx_v10_0_compute_mqd_init(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_compute_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000003;

	eop_base_addr = ring->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_EOP_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(GFX10_MEC_HPD_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);

	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* disable the queue if it's active */
	ring->wptr = 0;
	mqd->cp_hqd_dequeue_request = 0;
	mqd->cp_hqd_pq_rptr = 0;
	mqd->cp_hqd_pq_wptr_lo = 0;
	mqd->cp_hqd_pq_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = RREG32_SOC15(GC, 0, mmCP_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
			    (order_base_2(ring->ring_size / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			    ((order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1) << 8));
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ENDIAN_SWAP, 1);
#endif
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, TUNNEL_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	tmp = 0;
	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				DOORBELL_OFFSET, ring->doorbell_index);

		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_hqd_pq_rptr = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR);

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PERSISTENT_STATE);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MIN_IB_AVAIL_SIZE */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_IB_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 3);
	mqd->cp_hqd_ib_control = tmp;

	/* activate the queue */
	mqd->cp_hqd_active = 1;

	return 0;
}

static int gfx_v10_0_kiq_init_register(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_compute_mqd *mqd = ring->mqd_ptr;
	int j;

	/* disable wptr polling */
	WREG32_FIELD15(GC, 0, CP_PQ_WPTR_POLL_CNTL, EN, 0);

	/* write the EOP addr */
	WREG32_SOC15(GC, 0, mmCP_HQD_EOP_BASE_ADDR,
	       mqd->cp_hqd_eop_base_addr_lo);
	WREG32_SOC15(GC, 0, mmCP_HQD_EOP_BASE_ADDR_HI,
	       mqd->cp_hqd_eop_base_addr_hi);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	WREG32_SOC15(GC, 0, mmCP_HQD_EOP_CONTROL,
	       mqd->cp_hqd_eop_control);

	/* enable doorbell? */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1) {
		WREG32_SOC15(GC, 0, mmCP_HQD_DEQUEUE_REQUEST, 1);
		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}
		WREG32_SOC15(GC, 0, mmCP_HQD_DEQUEUE_REQUEST,
		       mqd->cp_hqd_dequeue_request);
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR,
		       mqd->cp_hqd_pq_rptr);
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_LO,
		       mqd->cp_hqd_pq_wptr_lo);
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_HI,
		       mqd->cp_hqd_pq_wptr_hi);
	}

	/* set the pointer to the MQD */
	WREG32_SOC15(GC, 0, mmCP_MQD_BASE_ADDR,
	       mqd->cp_mqd_base_addr_lo);
	WREG32_SOC15(GC, 0, mmCP_MQD_BASE_ADDR_HI,
	       mqd->cp_mqd_base_addr_hi);

	/* set MQD vmid to 0 */
	WREG32_SOC15(GC, 0, mmCP_MQD_CONTROL,
	       mqd->cp_mqd_control);

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_BASE,
	       mqd->cp_hqd_pq_base_lo);
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_BASE_HI,
	       mqd->cp_hqd_pq_base_hi);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_CONTROL,
	       mqd->cp_hqd_pq_control);

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR_REPORT_ADDR,
		mqd->cp_hqd_pq_rptr_report_addr_lo);
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
		mqd->cp_hqd_pq_rptr_report_addr_hi);

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR,
	       mqd->cp_hqd_pq_wptr_poll_addr_lo);
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR_HI,
	       mqd->cp_hqd_pq_wptr_poll_addr_hi);

	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		WREG32_SOC15(GC, 0, mmCP_MEC_DOORBELL_RANGE_LOWER,
			(adev->doorbell_index.kiq * 2) << 2);
		WREG32_SOC15(GC, 0, mmCP_MEC_DOORBELL_RANGE_UPPER,
			(adev->doorbell_index.userqueue_end * 2) << 2);
	}

	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_LO,
	       mqd->cp_hqd_pq_wptr_lo);
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_HI,
	       mqd->cp_hqd_pq_wptr_hi);

	/* set the vmid for the queue */
	WREG32_SOC15(GC, 0, mmCP_HQD_VMID, mqd->cp_hqd_vmid);

	WREG32_SOC15(GC, 0, mmCP_HQD_PERSISTENT_STATE,
	       mqd->cp_hqd_persistent_state);

	/* activate the queue */
	WREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE,
	       mqd->cp_hqd_active);

	if (ring->use_doorbell)
		WREG32_FIELD15(GC, 0, CP_PQ_STATUS, DOORBELL_ENABLE, 1);

	return 0;
}

static int gfx_v10_0_kiq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_compute_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = AMDGPU_MAX_COMPUTE_RINGS;

	gfx_v10_0_kiq_setting(ring);

	if (adev->in_gpu_reset) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(*mqd));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);

		mutex_lock(&adev->srbm_mutex);
		nv_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v10_0_kiq_init_register(ring);
		nv_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	} else {
		memset((void *)mqd, 0, sizeof(*mqd));
		mutex_lock(&adev->srbm_mutex);
		nv_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v10_0_compute_mqd_init(ring);
		gfx_v10_0_kiq_init_register(ring);
		nv_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(*mqd));
	}

	return 0;
}

static int gfx_v10_0_kcq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_compute_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = ring - &adev->gfx.compute_ring[0];

	if (!adev->in_gpu_reset && !adev->in_suspend) {
		memset((void *)mqd, 0, sizeof(*mqd));
		mutex_lock(&adev->srbm_mutex);
		nv_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v10_0_compute_mqd_init(ring);
		nv_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(*mqd));
	} else if (adev->in_gpu_reset) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(*mqd));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);
	} else {
		amdgpu_ring_clear_ring(ring);
	}

	return 0;
}

static int gfx_v10_0_kiq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->gfx.kiq.ring;

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
	if (unlikely(r != 0))
		return r;

	gfx_v10_0_kiq_init_queue(ring);
	amdgpu_bo_kunmap(ring->mqd_obj);
	ring->mqd_ptr = NULL;
	amdgpu_bo_unreserve(ring->mqd_obj);
	ring->sched.ready = true;
	return 0;
}

static int gfx_v10_0_kcq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = NULL;
	int r = 0, i;

	gfx_v10_0_cp_compute_enable(adev, true);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;
		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
		if (!r) {
			r = gfx_v10_0_kcq_init_queue(ring);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}

	r = amdgpu_gfx_enable_kcq(adev);
done:
	return r;
}

static int gfx_v10_0_cp_resume(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *ring;

	if (!(adev->flags & AMD_IS_APU))
		gfx_v10_0_enable_gui_idle_interrupt(adev, false);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		/* legacy firmware loading */
		r = gfx_v10_0_cp_gfx_load_microcode(adev);
		if (r)
			return r;

		r = gfx_v10_0_cp_compute_load_microcode(adev);
		if (r)
			return r;
	}

	r = gfx_v10_0_kiq_resume(adev);
	if (r)
		return r;

	r = gfx_v10_0_kcq_resume(adev);
	if (r)
		return r;

	if (!amdgpu_async_gfx_ring) {
		r = gfx_v10_0_cp_gfx_resume(adev);
		if (r)
			return r;
	} else {
		r = gfx_v10_0_cp_async_gfx_ring_resume(adev);
		if (r)
			return r;
	}

	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		DRM_INFO("gfx %d ring me %d pipe %d q %d\n",
			 i, ring->me, ring->pipe, ring->queue);
		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->sched.ready = false;
			return r;
		}
	}

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		ring->sched.ready = true;
		DRM_INFO("compute ring %d mec %d pipe %d q %d\n",
			 i, ring->me, ring->pipe, ring->queue);
		r = amdgpu_ring_test_ring(ring);
		if (r)
			ring->sched.ready = false;
	}

	return 0;
}

static void gfx_v10_0_cp_enable(struct amdgpu_device *adev, bool enable)
{
	gfx_v10_0_cp_gfx_enable(adev, enable);
	gfx_v10_0_cp_compute_enable(adev, enable);
}

static bool gfx_v10_0_check_grbm_cam_remapping(struct amdgpu_device *adev)
{
	uint32_t data, pattern = 0xDEADBEEF;

	/* check if mmVGT_ESGS_RING_SIZE_UMD
	 * has been remapped to mmVGT_ESGS_RING_SIZE */
	data = RREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE);

	WREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE, 0);

	WREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE_UMD, pattern);

	if (RREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE) == pattern) {
		WREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE_UMD, data);
		return true;
	} else {
		WREG32_SOC15(GC, 0, mmVGT_ESGS_RING_SIZE, data);
		return false;
	}
}

static void gfx_v10_0_setup_grbm_cam_remapping(struct amdgpu_device *adev)
{
	uint32_t data;

	/* initialize cam_index to 0
	 * index will auto-inc after each data writting */
	WREG32_SOC15(GC, 0, mmGRBM_CAM_INDEX, 0);

	/* mmVGT_TF_RING_SIZE_UMD -> mmVGT_TF_RING_SIZE */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_RING_SIZE_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_RING_SIZE) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmVGT_TF_MEMORY_BASE_UMD -> mmVGT_TF_MEMORY_BASE */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_MEMORY_BASE_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_MEMORY_BASE) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmVGT_TF_MEMORY_BASE_HI_UMD -> mmVGT_TF_MEMORY_BASE_HI */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_MEMORY_BASE_HI_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_TF_MEMORY_BASE_HI) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmVGT_HS_OFFCHIP_PARAM_UMD -> mmVGT_HS_OFFCHIP_PARAM */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_HS_OFFCHIP_PARAM_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_HS_OFFCHIP_PARAM) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmVGT_ESGS_RING_SIZE_UMD -> mmVGT_ESGS_RING_SIZE */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_ESGS_RING_SIZE_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_ESGS_RING_SIZE) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmVGT_GSVS_RING_SIZE_UMD -> mmVGT_GSVS_RING_SIZE */
	data = (SOC15_REG_OFFSET(GC, 0, mmVGT_GSVS_RING_SIZE_UMD) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmVGT_GSVS_RING_SIZE) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);

	/* mmSPI_CONFIG_CNTL_REMAP -> mmSPI_CONFIG_CNTL */
	data = (SOC15_REG_OFFSET(GC, 0, mmSPI_CONFIG_CNTL_REMAP) <<
		GRBM_CAM_DATA__CAM_ADDR__SHIFT) |
	       (SOC15_REG_OFFSET(GC, 0, mmSPI_CONFIG_CNTL) <<
		GRBM_CAM_DATA__CAM_REMAPADDR__SHIFT);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA_UPPER, 0);
	WREG32_SOC15(GC, 0, mmGRBM_CAM_DATA, data);
}

static int gfx_v10_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gfx_v10_0_csb_vram_pin(adev);
	if (r)
		return r;

	if (!amdgpu_emu_mode)
		gfx_v10_0_init_golden_registers(adev);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		/**
		 * For gfx 10, rlc firmware loading relies on smu firmware is
		 * loaded firstly, so in direct type, it has to load smc ucode
		 * here before rlc.
		 */
		r = smu_load_microcode(&adev->smu);
		if (r)
			return r;

		r = smu_check_fw_status(&adev->smu);
		if (r) {
			pr_err("SMC firmware status is not correct\n");
			return r;
		}
	}

	/* if GRBM CAM not remapped, set up the remapping */
	if (!gfx_v10_0_check_grbm_cam_remapping(adev))
		gfx_v10_0_setup_grbm_cam_remapping(adev);

	gfx_v10_0_constants_init(adev);

	r = gfx_v10_0_rlc_resume(adev);
	if (r)
		return r;

	/*
	 * init golden registers and rlc resume may override some registers,
	 * reconfig them here
	 */
	gfx_v10_0_tcp_harvest(adev);

	r = gfx_v10_0_cp_resume(adev);
	if (r)
		return r;

	return r;
}

#ifndef BRING_UP_DEBUG
static int gfx_v10_0_kiq_disable_kgq(struct amdgpu_device *adev)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	int i;

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size *
					adev->gfx.num_gfx_rings))
		return -ENOMEM;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		kiq->pmf->kiq_unmap_queues(kiq_ring, &adev->gfx.gfx_ring[i],
					   PREEMPT_QUEUES, 0, 0);

	return amdgpu_ring_test_ring(kiq_ring);
}
#endif

static int gfx_v10_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);
#ifndef BRING_UP_DEBUG
	if (amdgpu_async_gfx_ring) {
		r = gfx_v10_0_kiq_disable_kgq(adev);
		if (r)
			DRM_ERROR("KGQ disable failed\n");
	}
#endif
	if (amdgpu_gfx_disable_kcq(adev))
		DRM_ERROR("KCQ disable failed\n");
	if (amdgpu_sriov_vf(adev)) {
		pr_debug("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}
	gfx_v10_0_cp_enable(adev, false);
	gfx_v10_0_enable_gui_idle_interrupt(adev, false);
	gfx_v10_0_csb_vram_unpin(adev);

	return 0;
}

static int gfx_v10_0_suspend(void *handle)
{
	return gfx_v10_0_hw_fini(handle);
}

static int gfx_v10_0_resume(void *handle)
{
	return gfx_v10_0_hw_init(handle);
}

static bool gfx_v10_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (REG_GET_FIELD(RREG32_SOC15(GC, 0, mmGRBM_STATUS),
				GRBM_STATUS, GUI_ACTIVE))
		return false;
	else
		return true;
}

static int gfx_v10_0_wait_for_idle(void *handle)
{
	unsigned i;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS) &
			GRBM_STATUS__GUI_ACTIVE_MASK;

		if (!REG_GET_FIELD(tmp, GRBM_STATUS, GUI_ACTIVE))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v10_0_soft_reset(void *handle)
{
	u32 grbm_soft_reset = 0;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* GRBM_STATUS */
	tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS);
	if (tmp & (GRBM_STATUS__PA_BUSY_MASK | GRBM_STATUS__SC_BUSY_MASK |
		   GRBM_STATUS__BCI_BUSY_MASK | GRBM_STATUS__SX_BUSY_MASK |
		   GRBM_STATUS__TA_BUSY_MASK | GRBM_STATUS__DB_BUSY_MASK |
		   GRBM_STATUS__CB_BUSY_MASK | GRBM_STATUS__GDS_BUSY_MASK |
		   GRBM_STATUS__SPI_BUSY_MASK | GRBM_STATUS__GE_BUSY_NO_DMA_MASK
		   | GRBM_STATUS__BCI_BUSY_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP,
						1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_GFX,
						1);
	}

	if (tmp & (GRBM_STATUS__CP_BUSY_MASK | GRBM_STATUS__CP_COHERENCY_BUSY_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP,
						1);
	}

	/* GRBM_STATUS2 */
	tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS2);
	if (REG_GET_FIELD(tmp, GRBM_STATUS2, RLC_BUSY))
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_RLC,
						1);

	if (grbm_soft_reset) {
		/* stop the rlc */
		gfx_v10_0_rlc_stop(adev);

		/* Disable GFX parsing/prefetching */
		gfx_v10_0_cp_gfx_enable(adev, false);

		/* Disable MEC parsing/prefetching */
		gfx_v10_0_cp_compute_enable(adev, false);

		if (grbm_soft_reset) {
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
			tmp |= grbm_soft_reset;
			dev_info(adev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
			WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);

			udelay(50);

			tmp &= ~grbm_soft_reset;
			WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
		}

		/* Wait a little for things to settle down */
		udelay(50);
	}
	return 0;
}

static uint64_t gfx_v10_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32_SOC15(GC, 0, mmRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32_SOC15(GC, 0, mmRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32_SOC15(GC, 0, mmRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	return clock;
}

static void gfx_v10_0_ring_emit_gds_switch(struct amdgpu_ring *ring,
					   uint32_t vmid,
					   uint32_t gds_base, uint32_t gds_size,
					   uint32_t gws_base, uint32_t gws_size,
					   uint32_t oa_base, uint32_t oa_size)
{
	struct amdgpu_device *adev = ring->adev;

	/* GDS Base */
	gfx_v10_0_write_data_to_reg(ring, 0, false,
				    SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_BASE) + 2 * vmid,
				    gds_base);

	/* GDS Size */
	gfx_v10_0_write_data_to_reg(ring, 0, false,
				    SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_SIZE) + 2 * vmid,
				    gds_size);

	/* GWS */
	gfx_v10_0_write_data_to_reg(ring, 0, false,
				    SOC15_REG_OFFSET(GC, 0, mmGDS_GWS_VMID0) + vmid,
				    gws_size << GDS_GWS_VMID0__SIZE__SHIFT | gws_base);

	/* OA */
	gfx_v10_0_write_data_to_reg(ring, 0, false,
				    SOC15_REG_OFFSET(GC, 0, mmGDS_OA_VMID0) + vmid,
				    (1 << (oa_size + oa_base)) - (1 << oa_base));
}

static int gfx_v10_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfx.num_gfx_rings = GFX10_NUM_GFX_RINGS;
	adev->gfx.num_compute_rings = AMDGPU_MAX_COMPUTE_RINGS;

	gfx_v10_0_set_kiq_pm4_funcs(adev);
	gfx_v10_0_set_ring_funcs(adev);
	gfx_v10_0_set_irq_funcs(adev);
	gfx_v10_0_set_gds_init(adev);
	gfx_v10_0_set_rlc_funcs(adev);

	return 0;
}

static int gfx_v10_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	return 0;
}

static bool gfx_v10_0_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_cntl;

	/* if RLC is not enabled, do nothing */
	rlc_cntl = RREG32_SOC15(GC, 0, mmRLC_CNTL);
	return (REG_GET_FIELD(rlc_cntl, RLC_CNTL, RLC_ENABLE_F32)) ? true : false;
}

static void gfx_v10_0_set_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;
	unsigned i;

	data = RLC_SAFE_MODE__CMD_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
	WREG32_SOC15(GC, 0, mmRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SOC15(GC, 0, mmRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v10_0_unset_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RLC_SAFE_MODE__CMD_MASK;
	WREG32_SOC15(GC, 0, mmRLC_SAFE_MODE, data);
}

static void gfx_v10_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t data, def;

	/* It is disabled by HW by default */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		/* 1 - RLC_CGTT_MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		data &= ~(RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);

		/* only for Vega10 & Raven1 */
		data |= RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK;

		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* MGLS is a global flag to control all MGLS in GFX */
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			/* 2 - RLC memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS) {
				def = data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
				data |= RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL, data);
			}
			/* 3 - CP memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
				def = data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
				data |= CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL, data);
			}
		}
	} else {
		/* 1 - MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		data |= (RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 2 - disable MGLS in RLC */
		data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK) {
			data &= ~RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL, data);
		}

		/* 3 - disable MGLS in CP */
		data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
		if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK) {
			data &= ~CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL, data);
		}
	}
}

static void gfx_v10_0_update_3d_clock_gating(struct amdgpu_device *adev,
					   bool enable)
{
	uint32_t data, def;

	/* Enable 3D CGCG/CGLS */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		/* write cmd to clear cgcg/cgls ov */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		/* unset CGCG override */
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_GFX3D_CG_OVERRIDE_MASK;
		/* update CGCG and CGLS override bits */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);
		/* enable 3Dcgcg FSM(0x0000363f) */
		def = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);
		data = (0x36 << RLC_CGCG_CGLS_CTRL_3D__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
			RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGLS)
			data |= (0x000F << RLC_CGCG_CGLS_CTRL_3D__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK;
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D, data);

		/* set IDLE_POLL_COUNT(0x00900100) */
		def = RREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL);
		data = (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x0090 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		if (def != data)
			WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL, data);
	} else {
		/* Disable CGCG/CGLS */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);
		/* disable cgcg, cgls should be disabled */
		data &= ~(RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK |
			  RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK);
		/* disable cgcg and cgls in FSM */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D, data);
	}
}

static void gfx_v10_0_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t def, data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		/* unset CGCG override */
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGCG_OVERRIDE_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		else
			data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		/* update CGCG and CGLS override bits */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* enable cgcg FSM(0x0000363F) */
		def = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);
		data = (0x36 << RLC_CGCG_CGLS_CTRL__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
			RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data |= (0x000F << RLC_CGCG_CGLS_CTRL__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, data);

		/* set IDLE_POLL_COUNT(0x00900100) */
		def = RREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL);
		data = (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x0090 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		if (def != data)
			WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL, data);
	} else {
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);
		/* reset CGCG/CGLS bits */
		data &= ~(RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK | RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK);
		/* disable cgcg and cgls in FSM */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, data);
	}
}

static int gfx_v10_0_update_gfx_clock_gating(struct amdgpu_device *adev,
					    bool enable)
{
	amdgpu_gfx_rlc_enter_safe_mode(adev);

	if (enable) {
		/* CGCG/CGLS should be enabled after MGCG/MGLS
		 * ===  MGCG + MGLS ===
		 */
		gfx_v10_0_update_medium_grain_clock_gating(adev, enable);
		/* ===  CGCG /CGLS for GFX 3D Only === */
		gfx_v10_0_update_3d_clock_gating(adev, enable);
		/* ===  CGCG + CGLS === */
		gfx_v10_0_update_coarse_grain_clock_gating(adev, enable);
	} else {
		/* CGCG/CGLS should be disabled before MGCG/MGLS
		 * ===  CGCG + CGLS ===
		 */
		gfx_v10_0_update_coarse_grain_clock_gating(adev, enable);
		/* ===  CGCG /CGLS for GFX 3D Only === */
		gfx_v10_0_update_3d_clock_gating(adev, enable);
		/* ===  MGCG + MGLS === */
		gfx_v10_0_update_medium_grain_clock_gating(adev, enable);
	}

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_CGLS |
	     AMD_CG_SUPPORT_GFX_CGCG |
	     AMD_CG_SUPPORT_GFX_CGLS |
	     AMD_CG_SUPPORT_GFX_3D_CGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGLS))
		gfx_v10_0_enable_gui_idle_interrupt(adev, enable);

	amdgpu_gfx_rlc_exit_safe_mode(adev);

	return 0;
}

static const struct amdgpu_rlc_funcs gfx_v10_0_rlc_funcs = {
	.is_rlc_enabled = gfx_v10_0_is_rlc_enabled,
	.set_safe_mode = gfx_v10_0_set_safe_mode,
	.unset_safe_mode = gfx_v10_0_unset_safe_mode,
	.init = gfx_v10_0_rlc_init,
	.get_csb_size = gfx_v10_0_get_csb_size,
	.get_csb_buffer = gfx_v10_0_get_csb_buffer,
	.resume = gfx_v10_0_rlc_resume,
	.stop = gfx_v10_0_rlc_stop,
	.reset = gfx_v10_0_rlc_reset,
	.start = gfx_v10_0_rlc_start
};

static int gfx_v10_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_PG_STATE_GATE) ? true : false;
	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
		if (!enable) {
			amdgpu_gfx_off_ctrl(adev, false);
			cancel_delayed_work_sync(&adev->gfx.gfx_off_delay_work);
		} else
			amdgpu_gfx_off_ctrl(adev, true);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v10_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		gfx_v10_0_update_gfx_clock_gating(adev,
						 state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}
	return 0;
}

static void gfx_v10_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	/* AMD_CG_SUPPORT_GFX_MGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_MGCG;

	/* AMD_CG_SUPPORT_GFX_CGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);
	if (data & RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGCG;

	/* AMD_CG_SUPPORT_GFX_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGLS;

	/* AMD_CG_SUPPORT_GFX_RLC_LS */
	data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
	if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_RLC_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_CP_LS */
	data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
	if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CP_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_3D_CGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);
	if (data & RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_3D_CGCG;

	/* AMD_CG_SUPPORT_GFX_3D_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_3D_CGLS;
}

static u64 gfx_v10_0_ring_get_rptr_gfx(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs]; /* gfx10 is 32bit rptr*/
}

static u64 gfx_v10_0_ring_get_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		wptr = atomic64_read((atomic64_t *)&adev->wb.wb[ring->wptr_offs]);
	} else {
		wptr = RREG32_SOC15(GC, 0, mmCP_RB0_WPTR);
		wptr += (u64)RREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI) << 32;
	}

	return wptr;
}

static void gfx_v10_0_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		atomic64_set((atomic64_t *)&adev->wb.wb[ring->wptr_offs], ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		WREG32_SOC15(GC, 0, mmCP_RB0_WPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI, upper_32_bits(ring->wptr));
	}
}

static u64 gfx_v10_0_ring_get_rptr_compute(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs]; /* gfx10 hardware is 32bit rptr */
}

static u64 gfx_v10_0_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell)
		wptr = atomic64_read((atomic64_t *)&ring->adev->wb.wb[ring->wptr_offs]);
	else
		BUG();
	return wptr;
}

static void gfx_v10_0_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		atomic64_set((atomic64_t *)&adev->wb.wb[ring->wptr_offs], ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		BUG(); /* only DOORBELL method supported on gfx10 now */
	}
}

static void gfx_v10_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask, reg_mem_engine;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio_funcs->hdp_flush_reg;

	if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) {
		switch (ring->me) {
		case 1:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp2 << ring->pipe;
			break;
		case 2:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp6 << ring->pipe;
			break;
		default:
			return;
		}
		reg_mem_engine = 0;
	} else {
		ref_and_mask = nbio_hf_reg->ref_and_mask_cp0;
		reg_mem_engine = 1; /* pfp */
	}

	gfx_v10_0_wait_reg_mem(ring, reg_mem_engine, 0, 1,
			       adev->nbio_funcs->get_hdp_flush_req_offset(adev),
			       adev->nbio_funcs->get_hdp_flush_done_offset(adev),
			       ref_and_mask, ref_and_mask, 0x20);
}

static void gfx_v10_0_ring_emit_ib_gfx(struct amdgpu_ring *ring,
				       struct amdgpu_job *job,
				       struct amdgpu_ib *ib,
				       uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 header, control = 0;

	if (ib->flags & AMDGPU_IB_FLAG_CE)
		header = PACKET3(PACKET3_INDIRECT_BUFFER_CNST, 2);
	else
		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vmid << 24);

	if (amdgpu_mcbp && (ib->flags & AMDGPU_IB_FLAG_PREEMPT)) {
		control |= INDIRECT_BUFFER_PRE_ENB(1);

		if (flags & AMDGPU_IB_PREEMPTED)
			control |= INDIRECT_BUFFER_PRE_RESUME(1);

		if (!(ib->flags & AMDGPU_IB_FLAG_CE))
			gfx_v10_0_ring_emit_de_meta(ring,
				    flags & AMDGPU_IB_PREEMPTED ? true : false);
	}

	amdgpu_ring_write(ring, header);
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
		(2 << 0) |
#endif
		lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v10_0_ring_emit_ib_compute(struct amdgpu_ring *ring,
					   struct amdgpu_job *job,
					   struct amdgpu_ib *ib,
					   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 control = INDIRECT_BUFFER_VALID | ib->length_dw | (vmid << 24);

	/* Currently, there is a high possibility to get wave ID mismatch
	 * between ME and GDS, leading to a hw deadlock, because ME generates
	 * different wave IDs than the GDS expects. This situation happens
	 * randomly when at least 5 compute pipes use GDS ordered append.
	 * The wave IDs generated by ME are also wrong after suspend/resume.
	 * Those are probably bugs somewhere else in the kernel driver.
	 *
	 * Writing GDS_COMPUTE_MAX_WAVE_ID resets wave ID counters in ME and
	 * GDS to 0 for this ring (me/pipe).
	 */
	if (ib->flags & AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		amdgpu_ring_write(ring, mmGDS_COMPUTE_MAX_WAVE_ID);
		amdgpu_ring_write(ring, ring->adev->gds.gds_compute_max_wave_id);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
				(2 << 0) |
#endif
				lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v10_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				     u64 seq, unsigned flags)
{
	struct amdgpu_device *adev = ring->adev;
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;

	/* Interrupt not work fine on GFX10.1 model yet. Use fallback instead */
	if (adev->pdev->device == 0x50)
		int_sel = false;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 6));
	amdgpu_ring_write(ring, (PACKET3_RELEASE_MEM_GCR_SEQ |
				 PACKET3_RELEASE_MEM_GCR_GL2_WB |
				 PACKET3_RELEASE_MEM_GCR_GLM_INV | /* must be set with GLM_WB */
				 PACKET3_RELEASE_MEM_GCR_GLM_WB |
				 PACKET3_RELEASE_MEM_CACHE_POLICY(3) |
				 PACKET3_RELEASE_MEM_EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 PACKET3_RELEASE_MEM_EVENT_INDEX(5)));
	amdgpu_ring_write(ring, (PACKET3_RELEASE_MEM_DATA_SEL(write64bit ? 2 : 1) |
				 PACKET3_RELEASE_MEM_INT_SEL(int_sel ? 2 : 0)));

	/*
	 * the address should be Qword aligned if 64bit write, Dword
	 * aligned if only send 32bit data low (discard data high)
	 */
	if (write64bit)
		BUG_ON(addr & 0x7);
	else
		BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v10_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	gfx_v10_0_wait_reg_mem(ring, usepfp, 1, 0, lower_32_bits(addr),
			       upper_32_bits(addr), seq, 0xffffffff, 4);
}

static void gfx_v10_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* compute doesn't have PFP */
	if (ring->funcs->type == AMDGPU_RING_TYPE_GFX) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);
	}
}

static void gfx_v10_0_ring_emit_fence_kiq(struct amdgpu_ring *ring, u64 addr,
					  u64 seq, unsigned int flags)
{
	struct amdgpu_device *adev = ring->adev;

	/* we only allocate 32bit for each seq wb address */
	BUG_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	/* write fence seq to the "addr" */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(5) | WR_CONFIRM));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* set register to trigger INT */
		amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
		amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
					 WRITE_DATA_DST_SEL(0) | WR_CONFIRM));
		amdgpu_ring_write(ring, SOC15_REG_OFFSET(GC, 0, mmCPC_INT_STATUS));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, 0x20000000); /* src_id is 178 */
	}
}

static void gfx_v10_0_ring_emit_sb(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v10_0_ring_emit_cntxcntl(struct amdgpu_ring *ring, uint32_t flags)
{
	uint32_t dw2 = 0;

	if (amdgpu_mcbp)
		gfx_v10_0_ring_emit_ce_meta(ring,
				    flags & AMDGPU_IB_PREEMPTED ? true : false);

	gfx_v10_0_ring_emit_tmz(ring, true);

	dw2 |= 0x80000000; /* set load_enable otherwise this package is just NOPs */
	if (flags & AMDGPU_HAVE_CTX_SWITCH) {
		/* set load_global_config & load_global_uconfig */
		dw2 |= 0x8001;
		/* set load_cs_sh_regs */
		dw2 |= 0x01000000;
		/* set load_per_context_state & load_gfx_sh_regs for GFX */
		dw2 |= 0x10002;

		/* set load_ce_ram if preamble presented */
		if (AMDGPU_PREAMBLE_IB_PRESENT & flags)
			dw2 |= 0x10000000;
	} else {
		/* still load_ce_ram if this is the first time preamble presented
		 * although there is no context switch happens.
		 */
		if (AMDGPU_PREAMBLE_IB_PRESENT_FIRST & flags)
			dw2 |= 0x10000000;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, dw2);
	amdgpu_ring_write(ring, 0);
}

static unsigned gfx_v10_0_ring_emit_init_cond_exec(struct amdgpu_ring *ring)
{
	unsigned ret;

	amdgpu_ring_write(ring, PACKET3(PACKET3_COND_EXEC, 3));
	amdgpu_ring_write(ring, lower_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, 0); /* discard following DWs if *cond_exec_gpu_addr==0 */
	ret = ring->wptr & ring->buf_mask;
	amdgpu_ring_write(ring, 0x55aa55aa); /* patch dummy value later */

	return ret;
}

static void gfx_v10_0_ring_emit_patch_cond_exec(struct amdgpu_ring *ring, unsigned offset)
{
	unsigned cur;
	BUG_ON(offset > ring->buf_mask);
	BUG_ON(ring->ring[offset] != 0x55aa55aa);

	cur = (ring->wptr - 1) & ring->buf_mask;
	if (likely(cur > offset))
		ring->ring[offset] = cur - offset;
	else
		ring->ring[offset] = (ring->buf_mask + 1) - offset + cur;
}

static int gfx_v10_0_ring_preempt_ib(struct amdgpu_ring *ring)
{
	int i, r = 0;
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *kiq_ring = &kiq->ring;

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	if (amdgpu_ring_alloc(kiq_ring, kiq->pmf->unmap_queues_size))
		return -ENOMEM;

	/* assert preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, false);

	/* assert IB preemption, emit the trailing fence */
	kiq->pmf->kiq_unmap_queues(kiq_ring, ring, PREEMPT_QUEUES_NO_UNMAP,
				   ring->trail_fence_gpu_addr,
				   ++ring->trail_seq);
	amdgpu_ring_commit(kiq_ring);

	/* poll the trailing fence */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (ring->trail_seq ==
		    le32_to_cpu(*(ring->trail_fence_cpu_addr)))
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		r = -EINVAL;
		DRM_ERROR("ring %d failed to preempt ib\n", ring->idx);
	}

	/* deassert preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, true);
	return r;
}

static void gfx_v10_0_ring_emit_ce_meta(struct amdgpu_ring *ring, bool resume)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_ce_ib_state ce_payload = {0};
	uint64_t csa_addr;
	int cnt;

	cnt = (sizeof(ce_payload) >> 2) + 4 - 2;
	csa_addr = amdgpu_csa_vaddr(ring->adev);

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(2) |
				 WRITE_DATA_DST_SEL(8) |
				 WR_CONFIRM) |
				 WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(csa_addr +
			      offsetof(struct v10_gfx_meta_data, ce_payload)));
	amdgpu_ring_write(ring, upper_32_bits(csa_addr +
			      offsetof(struct v10_gfx_meta_data, ce_payload)));

	if (resume)
		amdgpu_ring_write_multiple(ring, adev->virt.csa_cpu_addr +
					   offsetof(struct v10_gfx_meta_data,
						    ce_payload),
					   sizeof(ce_payload) >> 2);
	else
		amdgpu_ring_write_multiple(ring, (void *)&ce_payload,
					   sizeof(ce_payload) >> 2);
}

static void gfx_v10_0_ring_emit_de_meta(struct amdgpu_ring *ring, bool resume)
{
	struct amdgpu_device *adev = ring->adev;
	struct v10_de_ib_state de_payload = {0};
	uint64_t csa_addr, gds_addr;
	int cnt;

	csa_addr = amdgpu_csa_vaddr(ring->adev);
	gds_addr = ALIGN(csa_addr + AMDGPU_CSA_SIZE - adev->gds.gds_size,
			 PAGE_SIZE);
	de_payload.gds_backup_addrlo = lower_32_bits(gds_addr);
	de_payload.gds_backup_addrhi = upper_32_bits(gds_addr);

	cnt = (sizeof(de_payload) >> 2) + 4 - 2;
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(8) |
				 WR_CONFIRM) |
				 WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(csa_addr +
			      offsetof(struct v10_gfx_meta_data, de_payload)));
	amdgpu_ring_write(ring, upper_32_bits(csa_addr +
			      offsetof(struct v10_gfx_meta_data, de_payload)));

	if (resume)
		amdgpu_ring_write_multiple(ring, adev->virt.csa_cpu_addr +
					   offsetof(struct v10_gfx_meta_data,
						    de_payload),
					   sizeof(de_payload) >> 2);
	else
		amdgpu_ring_write_multiple(ring, (void *)&de_payload,
					   sizeof(de_payload) >> 2);
}

static void gfx_v10_0_ring_emit_tmz(struct amdgpu_ring *ring, bool start)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_FRAME_CONTROL, 0));
	amdgpu_ring_write(ring, FRAME_CMD(start ? 0 : 1)); /* frame_end */
}

static void gfx_v10_0_ring_emit_rreg(struct amdgpu_ring *ring, uint32_t reg)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring, PACKET3(PACKET3_COPY_DATA, 4));
	amdgpu_ring_write(ring, 0 |	/* src: register*/
				(5 << 8) |	/* dst: memory */
				(1 << 20));	/* write confirm */
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, lower_32_bits(adev->wb.gpu_addr +
				adev->virt.reg_val_offs * 4));
	amdgpu_ring_write(ring, upper_32_bits(adev->wb.gpu_addr +
				adev->virt.reg_val_offs * 4));
}

static void gfx_v10_0_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				   uint32_t val)
{
	uint32_t cmd = 0;

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		cmd = WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM;
		break;
	case AMDGPU_RING_TYPE_KIQ:
		cmd = (1 << 16); /* no inc addr */
		break;
	default:
		cmd = WR_CONFIRM;
		break;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, cmd);
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v10_0_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask)
{
	gfx_v10_0_wait_reg_mem(ring, 0, 0, 0, reg, 0, val, mask, 0x20);
}

static void
gfx_v10_0_set_gfx_eop_interrupt_state(struct amdgpu_device *adev,
				      uint32_t me, uint32_t pipe,
				      enum amdgpu_interrupt_state state)
{
	uint32_t cp_int_cntl, cp_int_cntl_reg;

	if (!me) {
		switch (pipe) {
		case 0:
			cp_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_INT_CNTL_RING0);
			break;
		case 1:
			cp_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_INT_CNTL_RING1);
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(cp_int_cntl_reg);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    TIME_STAMP_INT_ENABLE, 0);
		WREG32(cp_int_cntl_reg, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(cp_int_cntl_reg);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    TIME_STAMP_INT_ENABLE, 1);
		WREG32(cp_int_cntl_reg, cp_int_cntl);
		break;
	default:
		break;
	}
}

static void gfx_v10_0_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
						     int me, int pipe,
						     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE0_INT_CNTL);
			break;
		case 1:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE1_INT_CNTL);
			break;
		case 2:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE2_INT_CNTL);
			break;
		case 3:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE3_INT_CNTL);
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 0);
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 1);
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	default:
		break;
	}
}

static int gfx_v10_0_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP:
		gfx_v10_0_set_gfx_eop_interrupt_state(adev, 0, 0, state);
		break;
	case AMDGPU_CP_IRQ_GFX_ME0_PIPE1_EOP:
		gfx_v10_0_set_gfx_eop_interrupt_state(adev, 0, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 1, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 1, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 1, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 1, 3, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 2, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 2, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 2, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP:
		gfx_v10_0_set_compute_eop_interrupt_state(adev, 2, 3, state);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v10_0_eop_irq(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *source,
			     struct amdgpu_iv_entry *entry)
{
	int i;
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;

	DRM_DEBUG("IH: CP EOP\n");
	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		if (pipe_id == 0)
			amdgpu_fence_process(&adev->gfx.gfx_ring[0]);
		else
			amdgpu_fence_process(&adev->gfx.gfx_ring[1]);
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			/* Per-queue interrupt is supported for MEC starting from VI.
			  * The interrupt can only be enabled/disabled per pipe instead of per queue.
			  */
			if ((ring->me == me_id) && (ring->pipe == pipe_id) && (ring->queue == queue_id))
				amdgpu_fence_process(ring);
		}
		break;
	}
	return 0;
}

static int gfx_v10_0_set_priv_reg_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
			       PRIV_REG_INT_ENABLE,
			       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v10_0_set_priv_inst_fault_state(struct amdgpu_device *adev,
					       struct amdgpu_irq_src *source,
					       unsigned type,
					       enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
			       PRIV_INSTR_INT_ENABLE,
			       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	default:
		break;
	}

	return 0;
}

static void gfx_v10_0_handle_priv_fault(struct amdgpu_device *adev,
					struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i;

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
			ring = &adev->gfx.gfx_ring[i];
			/* we only enabled 1 gfx queue per pipe for now */
			if (ring->me == me_id && ring->pipe == pipe_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			if (ring->me == me_id && ring->pipe == pipe_id &&
			    ring->queue == queue_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	default:
		BUG();
	}
}

static int gfx_v10_0_priv_reg_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	gfx_v10_0_handle_priv_fault(adev, entry);
	return 0;
}

static int gfx_v10_0_priv_inst_irq(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	gfx_v10_0_handle_priv_fault(adev, entry);
	return 0;
}

static int gfx_v10_0_kiq_set_interrupt_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *src,
					     unsigned int type,
					     enum amdgpu_interrupt_state state)
{
	uint32_t tmp, target;
	struct amdgpu_ring *ring = &(adev->gfx.kiq.ring);

	if (ring->me == 1)
		target = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE0_INT_CNTL);
	else
		target = SOC15_REG_OFFSET(GC, 0, mmCP_ME2_PIPE0_INT_CNTL);
	target += ring->pipe;

	switch (type) {
	case AMDGPU_CP_KIQ_IRQ_DRIVER0:
		if (state == AMDGPU_IRQ_STATE_DISABLE) {
			tmp = RREG32_SOC15(GC, 0, mmCPC_INT_CNTL);
			tmp = REG_SET_FIELD(tmp, CPC_INT_CNTL,
					    GENERIC2_INT_ENABLE, 0);
			WREG32_SOC15(GC, 0, mmCPC_INT_CNTL, tmp);

			tmp = RREG32(target);
			tmp = REG_SET_FIELD(tmp, CP_ME2_PIPE0_INT_CNTL,
					    GENERIC2_INT_ENABLE, 0);
			WREG32(target, tmp);
		} else {
			tmp = RREG32_SOC15(GC, 0, mmCPC_INT_CNTL);
			tmp = REG_SET_FIELD(tmp, CPC_INT_CNTL,
					    GENERIC2_INT_ENABLE, 1);
			WREG32_SOC15(GC, 0, mmCPC_INT_CNTL, tmp);

			tmp = RREG32(target);
			tmp = REG_SET_FIELD(tmp, CP_ME2_PIPE0_INT_CNTL,
					    GENERIC2_INT_ENABLE, 1);
			WREG32(target, tmp);
		}
		break;
	default:
		BUG(); /* kiq only support GENERIC2_INT now */
		break;
	}
	return 0;
}

static int gfx_v10_0_kiq_irq(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *source,
			     struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring = &(adev->gfx.kiq.ring);

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;
	DRM_DEBUG("IH: CPC GENERIC2_INT, me:%d, pipe:%d, queue:%d\n",
		   me_id, pipe_id, queue_id);

	amdgpu_fence_process(ring);
	return 0;
}

static const struct amd_ip_funcs gfx_v10_0_ip_funcs = {
	.name = "gfx_v10_0",
	.early_init = gfx_v10_0_early_init,
	.late_init = gfx_v10_0_late_init,
	.sw_init = gfx_v10_0_sw_init,
	.sw_fini = gfx_v10_0_sw_fini,
	.hw_init = gfx_v10_0_hw_init,
	.hw_fini = gfx_v10_0_hw_fini,
	.suspend = gfx_v10_0_suspend,
	.resume = gfx_v10_0_resume,
	.is_idle = gfx_v10_0_is_idle,
	.wait_for_idle = gfx_v10_0_wait_for_idle,
	.soft_reset = gfx_v10_0_soft_reset,
	.set_clockgating_state = gfx_v10_0_set_clockgating_state,
	.set_powergating_state = gfx_v10_0_set_powergating_state,
	.get_clockgating_state = gfx_v10_0_get_clockgating_state,
};

static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_gfx = {
	.type = AMDGPU_RING_TYPE_GFX,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB_0,
	.get_rptr = gfx_v10_0_ring_get_rptr_gfx,
	.get_wptr = gfx_v10_0_ring_get_wptr_gfx,
	.set_wptr = gfx_v10_0_ring_set_wptr_gfx,
	.emit_frame_size = /* totally 242 maximum if 16 IBs */
		5 + /* COND_EXEC */
		7 + /* PIPELINE_SYNC */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* VM_FLUSH */
		8 + /* FENCE for VM_FLUSH */
		20 + /* GDS switch */
		4 + /* double SWITCH_BUFFER,
		     * the first COND_EXEC jump to the place
		     * just prior to this double SWITCH_BUFFER
		     */
		5 + /* COND_EXEC */
		7 + /* HDP_flush */
		4 + /* VGT_flush */
		14 + /*	CE_META */
		31 + /*	DE_META */
		3 + /* CNTX_CTRL */
		5 + /* HDP_INVL */
		8 + 8 + /* FENCE x2 */
		2, /* SWITCH_BUFFER */
	.emit_ib_size =	4, /* gfx_v10_0_ring_emit_ib_gfx */
	.emit_ib = gfx_v10_0_ring_emit_ib_gfx,
	.emit_fence = gfx_v10_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v10_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v10_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v10_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v10_0_ring_emit_hdp_flush,
	.test_ring = gfx_v10_0_ring_test_ring,
	.test_ib = gfx_v10_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_switch_buffer = gfx_v10_0_ring_emit_sb,
	.emit_cntxcntl = gfx_v10_0_ring_emit_cntxcntl,
	.init_cond_exec = gfx_v10_0_ring_emit_init_cond_exec,
	.patch_cond_exec = gfx_v10_0_ring_emit_patch_cond_exec,
	.preempt_ib = gfx_v10_0_ring_preempt_ib,
	.emit_tmz = gfx_v10_0_ring_emit_tmz,
	.emit_wreg = gfx_v10_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v10_0_ring_emit_reg_wait,
};

static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB_0,
	.get_rptr = gfx_v10_0_ring_get_rptr_compute,
	.get_wptr = gfx_v10_0_ring_get_wptr_compute,
	.set_wptr = gfx_v10_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v10_0_ring_emit_gds_switch */
		7 + /* gfx_v10_0_ring_emit_hdp_flush */
		5 + /* hdp invalidate */
		7 + /* gfx_v10_0_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v10_0_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v10_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v10_0_ring_emit_ib_compute */
	.emit_ib = gfx_v10_0_ring_emit_ib_compute,
	.emit_fence = gfx_v10_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v10_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v10_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v10_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v10_0_ring_emit_hdp_flush,
	.test_ring = gfx_v10_0_ring_test_ring,
	.test_ib = gfx_v10_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_wreg = gfx_v10_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v10_0_ring_emit_reg_wait,
};

static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_kiq = {
	.type = AMDGPU_RING_TYPE_KIQ,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB_0,
	.get_rptr = gfx_v10_0_ring_get_rptr_compute,
	.get_wptr = gfx_v10_0_ring_get_wptr_compute,
	.set_wptr = gfx_v10_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v10_0_ring_emit_gds_switch */
		7 + /* gfx_v10_0_ring_emit_hdp_flush */
		5 + /*hdp invalidate */
		7 + /* gfx_v10_0_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v10_0_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v10_0_ring_emit_fence_kiq x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v10_0_ring_emit_ib_compute */
	.emit_ib = gfx_v10_0_ring_emit_ib_compute,
	.emit_fence = gfx_v10_0_ring_emit_fence_kiq,
	.test_ring = gfx_v10_0_ring_test_ring,
	.test_ib = gfx_v10_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_rreg = gfx_v10_0_ring_emit_rreg,
	.emit_wreg = gfx_v10_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v10_0_ring_emit_reg_wait,
};

static void gfx_v10_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	adev->gfx.kiq.ring.funcs = &gfx_v10_0_ring_funcs_kiq;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		adev->gfx.gfx_ring[i].funcs = &gfx_v10_0_ring_funcs_gfx;

	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		adev->gfx.compute_ring[i].funcs = &gfx_v10_0_ring_funcs_compute;
}

static const struct amdgpu_irq_src_funcs gfx_v10_0_eop_irq_funcs = {
	.set = gfx_v10_0_set_eop_interrupt_state,
	.process = gfx_v10_0_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v10_0_priv_reg_irq_funcs = {
	.set = gfx_v10_0_set_priv_reg_fault_state,
	.process = gfx_v10_0_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v10_0_priv_inst_irq_funcs = {
	.set = gfx_v10_0_set_priv_inst_fault_state,
	.process = gfx_v10_0_priv_inst_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v10_0_kiq_irq_funcs = {
	.set = gfx_v10_0_kiq_set_interrupt_state,
	.process = gfx_v10_0_kiq_irq,
};

static void gfx_v10_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v10_0_eop_irq_funcs;

	adev->gfx.kiq.irq.num_types = AMDGPU_CP_KIQ_IRQ_LAST;
	adev->gfx.kiq.irq.funcs = &gfx_v10_0_kiq_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v10_0_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v10_0_priv_inst_irq_funcs;
}

static void gfx_v10_0_set_rlc_funcs(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		adev->gfx.rlc.funcs = &gfx_v10_0_rlc_funcs;
		break;
	default:
		break;
	}
}

static void gfx_v10_0_set_gds_init(struct amdgpu_device *adev)
{
	/* init asic gds info */
	switch (adev->asic_type) {
	case CHIP_NAVI10:
	default:
		adev->gds.gds_size = 0x10000;
		adev->gds.gds_compute_max_wave_id = 0x4ff;
		break;
	}

	adev->gds.gws_size = 64;
	adev->gds.oa_size = 16;
}

static void gfx_v10_0_set_user_wgp_inactive_bitmap_per_sh(struct amdgpu_device *adev,
							  u32 bitmap)
{
	u32 data;

	if (!bitmap)
		return;

	data = bitmap << GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_WGPS__SHIFT;
	data &= GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_WGPS_MASK;

	WREG32_SOC15(GC, 0, mmGC_USER_SHADER_ARRAY_CONFIG, data);
}

static u32 gfx_v10_0_get_wgp_active_bitmap_per_sh(struct amdgpu_device *adev)
{
	u32 data, wgp_bitmask;
	data = RREG32_SOC15(GC, 0, mmCC_GC_SHADER_ARRAY_CONFIG);
	data |= RREG32_SOC15(GC, 0, mmGC_USER_SHADER_ARRAY_CONFIG);

	data &= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_WGPS_MASK;
	data >>= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_WGPS__SHIFT;

	wgp_bitmask =
		amdgpu_gfx_create_bitmask(adev->gfx.config.max_cu_per_sh >> 1);

	return (~data) & wgp_bitmask;
}

static u32 gfx_v10_0_get_cu_active_bitmap_per_sh(struct amdgpu_device *adev)
{
	u32 wgp_idx, wgp_active_bitmap;
	u32 cu_bitmap_per_wgp, cu_active_bitmap;

	wgp_active_bitmap = gfx_v10_0_get_wgp_active_bitmap_per_sh(adev);
	cu_active_bitmap = 0;

	for (wgp_idx = 0; wgp_idx < 16; wgp_idx++) {
		/* if there is one WGP enabled, it means 2 CUs will be enabled */
		cu_bitmap_per_wgp = 3 << (2 * wgp_idx);
		if (wgp_active_bitmap & (1 << wgp_idx))
			cu_active_bitmap |= cu_bitmap_per_wgp;
	}

	return cu_active_bitmap;
}

static int gfx_v10_0_get_cu_info(struct amdgpu_device *adev,
				 struct amdgpu_cu_info *cu_info)
{
	int i, j, k, counter, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0;
	unsigned disable_masks[4 * 2];

	if (!adev || !cu_info)
		return -EINVAL;

	amdgpu_gfx_parse_disable_cu(disable_masks, 4, 2);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			ao_bitmap = 0;
			counter = 0;
			gfx_v10_0_select_se_sh(adev, i, j, 0xffffffff);
			if (i < 4 && j < 2)
				gfx_v10_0_set_user_wgp_inactive_bitmap_per_sh(
					adev, disable_masks[i * 2 + j]);
			bitmap = gfx_v10_0_get_cu_active_bitmap_per_sh(adev);
			cu_info->bitmap[i][j] = bitmap;

			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k++) {
				if (bitmap & mask) {
					if (counter < adev->gfx.config.max_cu_per_sh)
						ao_bitmap |= mask;
					counter++;
				}
				mask <<= 1;
			}
			active_cu_number += counter;
			if (i < 2 && j < 2)
				ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
			cu_info->ao_cu_bitmap[i][j] = ao_bitmap;
		}
	}
	gfx_v10_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
	cu_info->simd_per_cu = NUM_SIMD_PER_CU;

	return 0;
}

const struct amdgpu_ip_block_version gfx_v10_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 10,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v10_0_ip_funcs,
};