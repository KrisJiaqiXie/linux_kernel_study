/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-shared.h"
#include "iwl-op-mode.h"
#include "iwl-agn-hw.h"

/* private includes */
#include "iwl-fw-file.h"

/**
 * struct iwl_drv - drv common data
 * @fw: the iwl_fw structure
 * @shrd: pointer to common shared structure
 * @op_mode: the running op_mode
 * @fw_index: firmware revision to try loading
 * @firmware_name: composite filename of ucode file to load
 * @request_firmware_complete: the firmware has been obtained from user space
 */
struct iwl_drv {
	struct iwl_fw fw;

	struct iwl_shared *shrd;
	struct iwl_op_mode *op_mode;

	int fw_index;                   /* firmware we're trying to load */
	char firmware_name[25];         /* name of firmware file to load */

	struct completion request_firmware_complete;
};



/*
 * struct fw_sec: Just for the image parsing proccess.
 * For the fw storage we are using struct fw_desc.
 */
struct fw_sec {
	const void *data;		/* the sec data */
	size_t size;			/* section size */
	u32 offset;			/* offset of writing in the device */
};

static void iwl_free_fw_desc(struct iwl_drv *drv, struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(trans(drv)->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static void iwl_free_fw_img(struct iwl_drv *drv, struct fw_img *img)
{
	int i;
	for (i = 0; i < IWL_UCODE_SECTION_MAX; i++)
		iwl_free_fw_desc(drv, &img->sec[i]);
}

static void iwl_dealloc_ucode(struct iwl_drv *drv)
{
	int i;
	for (i = 0; i < IWL_UCODE_TYPE_MAX; i++)
		iwl_free_fw_img(drv, drv->fw.img + i);
}

static int iwl_alloc_fw_desc(struct iwl_drv *drv, struct fw_desc *desc,
		      struct fw_sec *sec)
{
	if (!sec || !sec->size) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(trans(drv)->dev, sec->size,
					  &desc->p_addr, GFP_KERNEL);
	if (!desc->v_addr)
		return -ENOMEM;

	desc->len = sec->size;
	desc->offset = sec->offset;
	memcpy(desc->v_addr, sec->data, sec->size);
	return 0;
}

static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context);

#define UCODE_EXPERIMENTAL_INDEX	100
#define UCODE_EXPERIMENTAL_TAG		"exp"

static int iwl_request_firmware(struct iwl_drv *drv, bool first)
{
	const struct iwl_cfg *cfg = cfg(drv);
	const char *name_pre = cfg->fw_name_pre;
	char tag[8];

	if (first) {
#ifdef CONFIG_IWLWIFI_DEBUG_EXPERIMENTAL_UCODE
		drv->fw_index = UCODE_EXPERIMENTAL_INDEX;
		strcpy(tag, UCODE_EXPERIMENTAL_TAG);
	} else if (drv->fw_index == UCODE_EXPERIMENTAL_INDEX) {
#endif
		drv->fw_index = cfg->ucode_api_max;
		sprintf(tag, "%d", drv->fw_index);
	} else {
		drv->fw_index--;
		sprintf(tag, "%d", drv->fw_index);
	}

	if (drv->fw_index < cfg->ucode_api_min) {
		IWL_ERR(drv, "no suitable firmware found!\n");
		return -ENOENT;
	}

	sprintf(drv->firmware_name, "%s%s%s", name_pre, tag, ".ucode");

	IWL_DEBUG_INFO(drv, "attempting to load firmware %s'%s'\n",
		       (drv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? "EXPERIMENTAL " : "",
		       drv->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, drv->firmware_name,
				       trans(drv)->dev,
				       GFP_KERNEL, drv, iwl_ucode_callback);
}

struct fw_img_parsing {
	struct fw_sec sec[IWL_UCODE_SECTION_MAX];
	int sec_counter;
};

/*
 * struct fw_sec_parsing: to extract fw section and it's offset from tlv
 */
struct fw_sec_parsing {
	__le32 offset;
	const u8 data[];
} __packed;

/**
 * struct iwl_tlv_calib_data - parse the default calib data from TLV
 *
 * @ucode_type: the uCode to which the following default calib relates.
 * @calib: default calibrations.
 */
struct iwl_tlv_calib_data {
	__le32 ucode_type;
	__le64 calib;
} __packed;

struct iwl_firmware_pieces {
	struct fw_img_parsing img[IWL_UCODE_TYPE_MAX];

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;
};

/*
 * These functions are just to extract uCode section data from the pieces
 * structure.
 */
static struct fw_sec *get_sec(struct iwl_firmware_pieces *pieces,
			      enum iwl_ucode_type type,
			      int  sec)
{
	return &pieces->img[type].sec[sec];
}

static void set_sec_data(struct iwl_firmware_pieces *pieces,
			 enum iwl_ucode_type type,
			 int sec,
			 const void *data)
{
	pieces->img[type].sec[sec].data = data;
}

static void set_sec_size(struct iwl_firmware_pieces *pieces,
			 enum iwl_ucode_type type,
			 int sec,
			 size_t size)
{
	pieces->img[type].sec[sec].size = size;
}

static size_t get_sec_size(struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type,
			   int sec)
{
	return pieces->img[type].sec[sec].size;
}

static void set_sec_offset(struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type,
			   int sec,
			   u32 offset)
{
	pieces->img[type].sec[sec].offset = offset;
}

/*
 * Gets uCode section from tlv.
 */
static int iwl_store_ucode_sec(struct iwl_firmware_pieces *pieces,
			       const void *data, enum iwl_ucode_type type,
			       int size)
{
	struct fw_img_parsing *img;
	struct fw_sec *sec;
	struct fw_sec_parsing *sec_parse;

	if (WARN_ON(!pieces || !data || type >= IWL_UCODE_TYPE_MAX))
		return -1;

	sec_parse = (struct fw_sec_parsing *)data;

	img = &pieces->img[type];
	sec = &img->sec[img->sec_counter];

	sec->offset = le32_to_cpu(sec_parse->offset);
	sec->data = sec_parse->data;

	++img->sec_counter;

	return 0;
}

static int iwl_set_default_calib(struct iwl_drv *drv, const u8 *data)
{
	struct iwl_tlv_calib_data *def_calib =
					(struct iwl_tlv_calib_data *)data;
	u32 ucode_type = le32_to_cpu(def_calib->ucode_type);
	if (ucode_type >= IWL_UCODE_TYPE_MAX) {
		IWL_ERR(drv, "Wrong ucode_type %u for default calibration.\n",
			ucode_type);
		return -EINVAL;
	}
	drv->fw.default_calib[ucode_type] = le64_to_cpu(def_calib->calib);
	return 0;
}

static int iwl_parse_v1_v2_firmware(struct iwl_drv *drv,
				    const struct firmware *ucode_raw,
				    struct iwl_firmware_pieces *pieces)
{
	struct iwl_ucode_header *ucode = (void *)ucode_raw->data;
	u32 api_ver, hdr_size, build;
	char buildstr[25];
	const u8 *src;

	drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(drv->fw.ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(drv, "File size too small!\n");
			return -EINVAL;
		}
		build = le32_to_cpu(ucode->u.v2.build);
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v2.inst_size));
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v2.data_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v2.init_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v2.init_data_size));
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(drv, "File size too small!\n");
			return -EINVAL;
		}
		build = 0;
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v1.inst_size));
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v1.data_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v1.init_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v1.init_data_size));
		src = ucode->u.v1.data;
		break;
	}

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (drv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(drv->fw.fw_version,
		 sizeof(drv->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(drv->fw.ucode_ver),
		 IWL_UCODE_MINOR(drv->fw.ucode_ver),
		 IWL_UCODE_API(drv->fw.ucode_ver),
		 IWL_UCODE_SERIAL(drv->fw.ucode_ver),
		 buildstr);

	/* Verify size of file vs. image size info in file's header */

	if (ucode_raw->size != hdr_size +
	    get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) +
	    get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) +
	    get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) +
	    get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA)) {

		IWL_ERR(drv,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		return -EINVAL;
	}


	set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, src);
	src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST);
	set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
		       IWLAGN_RTC_INST_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, src);
	src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA);
	set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
		       IWLAGN_RTC_DATA_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, src);
	src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST);
	set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
		       IWLAGN_RTC_INST_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, src);
	src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA);
	set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
		       IWLAGN_RTC_DATA_LOWER_BOUND);
	return 0;
}

static int iwl_parse_tlv_firmware(struct iwl_drv *drv,
				const struct firmware *ucode_raw,
				struct iwl_firmware_pieces *pieces,
				struct iwl_ucode_capabilities *capa)
{
	struct iwl_tlv_ucode_header *ucode = (void *)ucode_raw->data;
	struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	int wanted_alternative = iwlagn_mod_params.wanted_ucode_alternative;
	int tmp;
	u64 alternatives;
	u32 tlv_len;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;
	char buildstr[25];
	u32 build;

	if (len < sizeof(*ucode)) {
		IWL_ERR(drv, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(drv, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	/*
	 * Check which alternatives are present, and "downgrade"
	 * when the chosen alternative is not present, warning
	 * the user when that happens. Some files may not have
	 * any alternatives, so don't warn in that case.
	 */
	alternatives = le64_to_cpu(ucode->alternatives);
	tmp = wanted_alternative;
	if (wanted_alternative > 63)
		wanted_alternative = 63;
	while (wanted_alternative && !(alternatives & BIT(wanted_alternative)))
		wanted_alternative--;
	if (wanted_alternative && wanted_alternative != tmp)
		IWL_WARN(drv,
			 "uCode alternative %d not available, choosing %d\n",
			 tmp, wanted_alternative);

	drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
	build = le32_to_cpu(ucode->build);

	if (build)
		sprintf(buildstr, " build %u%s", build,
		       (drv->fw_index == UCODE_EXPERIMENTAL_INDEX)
				? " (EXP)" : "");
	else
		buildstr[0] = '\0';

	snprintf(drv->fw.fw_version,
		 sizeof(drv->fw.fw_version),
		 "%u.%u.%u.%u%s",
		 IWL_UCODE_MAJOR(drv->fw.ucode_ver),
		 IWL_UCODE_MINOR(drv->fw.ucode_ver),
		 IWL_UCODE_API(drv->fw.ucode_ver),
		 IWL_UCODE_SERIAL(drv->fw.ucode_ver),
		 buildstr);

	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		u16 tlv_alt;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le16_to_cpu(tlv->type);
		tlv_alt = le16_to_cpu(tlv->alternative);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(drv, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		/*
		 * Alternative 0 is always valid.
		 *
		 * Skip alternative TLVs that are not selected.
		 */
		if (tlv_alt != 0 && tlv_alt != wanted_alternative)
			continue;

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			set_sec_data(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_REGULAR,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_DATA:
			set_sec_data(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_REGULAR,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_INIT:
			set_sec_data(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_INIT,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			set_sec_data(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_INIT,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(drv, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			drv->fw.enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_WOWLAN_INST:
			set_sec_data(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_WOWLAN,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_WOWLAN_DATA:
			set_sec_data(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_WOWLAN,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((__le32 *)tlv_data);
			break;
		 case IWL_UCODE_TLV_SEC_RT:
			iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_REGULAR,
					    tlv_len);
			drv->fw.mvm_fw = true;
			break;
		case IWL_UCODE_TLV_SEC_INIT:
			iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_INIT,
					    tlv_len);
			drv->fw.mvm_fw = true;
			break;
		case IWL_UCODE_TLV_SEC_WOWLAN:
			iwl_store_ucode_sec(pieces, tlv_data, IWL_UCODE_WOWLAN,
					    tlv_len);
			drv->fw.mvm_fw = true;
			break;
		case IWL_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof(struct iwl_tlv_calib_data))
				goto invalid_tlv_len;
			if (iwl_set_default_calib(drv, tlv_data))
				goto tlv_error;
			break;
		case IWL_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			drv->fw.phy_config = le32_to_cpup((__le32 *)tlv_data);
			break;
		default:
			IWL_DEBUG_INFO(drv, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (len) {
		IWL_ERR(drv, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(drv, IWL_DL_FW, (u8 *)data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(drv, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
 tlv_error:
	iwl_print_hex_dump(drv, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

static int alloc_pci_desc(struct iwl_drv *drv,
			  struct iwl_firmware_pieces *pieces,
			  enum iwl_ucode_type type)
{
	int i;
	for (i = 0;
	     i < IWL_UCODE_SECTION_MAX && get_sec_size(pieces, type, i);
	     i++)
		if (iwl_alloc_fw_desc(drv, &(drv->fw.img[type].sec[i]),
						get_sec(pieces, type, i)))
			return -1;
	return 0;
}

static int validate_sec_sizes(struct iwl_drv *drv,
			      struct iwl_firmware_pieces *pieces,
			      const struct iwl_cfg *cfg)
{
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime inst size = %Zd\n",
		get_sec_size(pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime data size = %Zd\n",
		get_sec_size(pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_DATA));
	IWL_DEBUG_INFO(drv, "f/w package hdr init inst size = %Zd\n",
		get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr init data size = %Zd\n",
		get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA));

	/* Verify that uCode images will fit in card's SRAM. */
	if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) >
							cfg->max_inst_size) {
		IWL_ERR(drv, "uCode instr len %Zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
						IWL_UCODE_SECTION_INST));
		return -1;
	}

	if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) >
							cfg->max_data_size) {
		IWL_ERR(drv, "uCode data len %Zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
						IWL_UCODE_SECTION_DATA));
		return -1;
	}

	 if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) >
							cfg->max_inst_size) {
		IWL_ERR(drv, "uCode init instr len %Zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_INIT,
						IWL_UCODE_SECTION_INST));
		return -1;
	}

	if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA) >
							cfg->max_data_size) {
		IWL_ERR(drv, "uCode init data len %Zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
						IWL_UCODE_SECTION_DATA));
		return -1;
	}
	return 0;
}


/**
 * iwl_ucode_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_ucode_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_drv *drv = context;
	const struct iwl_cfg *cfg = cfg(drv);
	struct iwl_fw *fw = &drv->fw;
	struct iwl_ucode_header *ucode;
	int err;
	struct iwl_firmware_pieces pieces;
	const unsigned int api_max = cfg->ucode_api_max;
	unsigned int api_ok = cfg->ucode_api_ok;
	const unsigned int api_min = cfg->ucode_api_min;
	u32 api_ver;
	int i;

	fw->ucode_capa.max_probe_length = 200;
	fw->ucode_capa.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	if (!api_ok)
		api_ok = api_max;

	memset(&pieces, 0, sizeof(pieces));

	if (!ucode_raw) {
		if (drv->fw_index <= api_ok)
			IWL_ERR(drv,
				"request for firmware file '%s' failed.\n",
				drv->firmware_name);
		goto try_again;
	}

	IWL_DEBUG_INFO(drv, "Loaded firmware file '%s' (%zd bytes).\n",
		       drv->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(drv, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwl_parse_v1_v2_firmware(drv, ucode_raw, &pieces);
	else
		err = iwl_parse_tlv_firmware(drv, ucode_raw, &pieces,
					   &fw->ucode_capa);

	if (err)
		goto try_again;

	api_ver = IWL_UCODE_API(drv->fw.ucode_ver);

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	/* no api version check required for experimental uCode */
	if (drv->fw_index != UCODE_EXPERIMENTAL_INDEX) {
		if (api_ver < api_min || api_ver > api_max) {
			IWL_ERR(drv,
				"Driver unable to support your firmware API. "
				"Driver supports v%u, firmware is v%u.\n",
				api_max, api_ver);
			goto try_again;
		}

		if (api_ver < api_ok) {
			if (api_ok != api_max)
				IWL_ERR(drv, "Firmware has old API version, "
					"expected v%u through v%u, got v%u.\n",
					api_ok, api_max, api_ver);
			else
				IWL_ERR(drv, "Firmware has old API version, "
					"expected v%u, got v%u.\n",
					api_max, api_ver);
			IWL_ERR(drv, "New firmware can be obtained from "
				      "http://www.intellinuxwireless.org/.\n");
		}
	}

	IWL_INFO(drv, "loaded firmware version %s", drv->fw.fw_version);

	/*
	 * For any of the failures below (before allocating pci memory)
	 * we will try to load a version with a smaller API -- maybe the
	 * user just got a corrupted version of the latest API.
	 */

	IWL_DEBUG_INFO(drv, "f/w package hdr ucode version raw = 0x%x\n",
		       drv->fw.ucode_ver);
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime inst size = %Zd\n",
		get_sec_size(&pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime data size = %Zd\n",
		get_sec_size(&pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_DATA));
	IWL_DEBUG_INFO(drv, "f/w package hdr init inst size = %Zd\n",
		get_sec_size(&pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr init data size = %Zd\n",
		get_sec_size(&pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA));

	/* Verify that uCode images will fit in card's SRAM */
	if (get_sec_size(&pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) >
							cfg->max_inst_size) {
		IWL_ERR(drv, "uCode instr len %Zd too large to fit in\n",
			get_sec_size(&pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST));
		goto try_again;
	}

	if (get_sec_size(&pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) >
							cfg->max_data_size) {
		IWL_ERR(drv, "uCode data len %Zd too large to fit in\n",
			get_sec_size(&pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA));
		goto try_again;
	}

	/*
	 * In mvm uCode there is no difference between data and instructions
	 * sections.
	 */
	if (!fw->mvm_fw && validate_sec_sizes(drv, &pieces, cfg))
		goto try_again;

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs */
	for (i = 0; i < IWL_UCODE_TYPE_MAX; i++)
		if (alloc_pci_desc(drv, &pieces, i))
			goto err_pci_alloc;

	/* Now that we can no longer fail, copy information */

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	fw->init_evtlog_ptr = pieces.init_evtlog_ptr;
	if (pieces.init_evtlog_size)
		fw->init_evtlog_size = (pieces.init_evtlog_size - 16)/12;
	else
		fw->init_evtlog_size =
			cfg->base_params->max_event_log_size;
	fw->init_errlog_ptr = pieces.init_errlog_ptr;
	fw->inst_evtlog_ptr = pieces.inst_evtlog_ptr;
	if (pieces.inst_evtlog_size)
		fw->inst_evtlog_size = (pieces.inst_evtlog_size - 16)/12;
	else
		fw->inst_evtlog_size =
			cfg->base_params->max_event_log_size;
	fw->inst_errlog_ptr = pieces.inst_errlog_ptr;

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (fw->ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		fw->ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);
	complete(&drv->request_firmware_complete);

	drv->op_mode = iwl_dvm_ops.start(drv->shrd->trans, &drv->fw);

	if (!drv->op_mode)
		goto out_unbind;

	return;

 try_again:
	/* try next, if any */
	release_firmware(ucode_raw);
	if (iwl_request_firmware(drv, false))
		goto out_unbind;
	return;

 err_pci_alloc:
	IWL_ERR(drv, "failed to allocate pci memory\n");
	iwl_dealloc_ucode(drv);
	release_firmware(ucode_raw);
 out_unbind:
	complete(&drv->request_firmware_complete);
	device_release_driver(trans(drv)->dev);
}

int iwl_drv_start(struct iwl_shared *shrd,
		  struct iwl_trans *trans, const struct iwl_cfg *cfg)
{
	struct iwl_drv *drv;
	int ret;

	shrd->cfg = cfg;

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv) {
		dev_printk(KERN_ERR, trans->dev, "Couldn't allocate iwl_drv");
		return -ENOMEM;
	}
	drv->shrd = shrd;
	shrd->drv = drv;

	init_completion(&drv->request_firmware_complete);

	ret = iwl_request_firmware(drv, true);

	if (ret) {
		dev_printk(KERN_ERR, trans->dev, "Couldn't request the fw");
		kfree(drv);
		shrd->drv = NULL;
	}

	return ret;
}

void iwl_drv_stop(struct iwl_shared *shrd)
{
	struct iwl_drv *drv = shrd->drv;

	wait_for_completion(&drv->request_firmware_complete);

	/* op_mode can be NULL if its start failed */
	if (drv->op_mode)
		iwl_op_mode_stop(drv->op_mode);

	iwl_dealloc_ucode(drv);

	kfree(drv);
	shrd->drv = NULL;
}
