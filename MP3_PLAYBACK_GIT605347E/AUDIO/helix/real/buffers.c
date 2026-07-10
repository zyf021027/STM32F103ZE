/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * buffers.c - allocation and freeing of internal MP3 decoder buffers
 *
 * All memory allocation for the codec is done in this file, so if you don't want 
 *  to use other the default system malloc() and free() for heap management this is 
 *  the only file you'll need to change.
 **************************************************************************************/

//#include "hlxclib/stdlib.h"		/* for malloc, free */ 
#include <stdlib.h>
#include <string.h>
#include "coder.h"

/**************************************************************************************
 * Function:    ClearBuffer
 *
 * Description: fill buffer with 0's
 *
 * Inputs:      pointer to buffer
 *              number of bytes to fill with 0
 *
 * Outputs:     cleared buffer
 *
 * Return:      none
 *
 * Notes:       slow, platform-independent equivalent to memset(buf, 0, nBytes)
 **************************************************************************************/
static void ClearBuffer(void *buf, int nBytes)
{
	int i;
	unsigned char *cbuf = (unsigned char *)buf;

	for (i = 0; i < nBytes; i++)
		cbuf[i] = 0;

	return;
}

/**************************************************************************************
 * Function:    AllocateBuffers
 *
 * Description: allocate all the memory needed for the MP3 decoder
 *
 * Inputs:      none
 *
 * Outputs:     none
 *
 * Return:      pointer to MP3DecInfo structure (initialized with pointers to all 
 *                the internal buffers needed for decoding, all other members of 
 *                MP3DecInfo structure set to 0)
 *
 * Notes:       if one or more mallocs fail, function frees any buffers already
 *                allocated before returning
 **************************************************************************************/
static MP3DecInfo g_mp3_dec_info;
static FrameHeader g_frame_header;
static SideInfo g_side_info;
static ScaleFactorInfo g_scale_factor_info;
static HuffmanInfo g_huffman_info;
static DequantInfo g_dequant_info;
static IMDCTInfo g_imdct_info;
static SubbandInfo g_subband_info;

MP3DecInfo *AllocateBuffers(void)
{
	ClearBuffer(&g_mp3_dec_info, sizeof(g_mp3_dec_info));
	ClearBuffer(&g_frame_header, sizeof(g_frame_header));
	ClearBuffer(&g_side_info, sizeof(g_side_info));
	ClearBuffer(&g_scale_factor_info, sizeof(g_scale_factor_info));
	ClearBuffer(&g_huffman_info, sizeof(g_huffman_info));
	ClearBuffer(&g_dequant_info, sizeof(g_dequant_info));
	ClearBuffer(&g_imdct_info, sizeof(g_imdct_info));
	ClearBuffer(&g_subband_info, sizeof(g_subband_info));

	g_mp3_dec_info.FrameHeaderPS = (void *)&g_frame_header;
	g_mp3_dec_info.SideInfoPS = (void *)&g_side_info;
	g_mp3_dec_info.ScaleFactorInfoPS = (void *)&g_scale_factor_info;
	g_mp3_dec_info.HuffmanInfoPS = (void *)&g_huffman_info;
	g_mp3_dec_info.DequantInfoPS = (void *)&g_dequant_info;
	g_mp3_dec_info.IMDCTInfoPS = (void *)&g_imdct_info;
	g_mp3_dec_info.SubbandInfoPS = (void *)&g_subband_info;

	return &g_mp3_dec_info;
}
#define SAFE_FREE(x)	{if (x)	free(x);	(x) = 0;}	/* helper macro */

/**************************************************************************************
 * Function:    FreeBuffers
 *
 * Description: frees all the memory used by the MP3 decoder
 *
 * Inputs:      pointer to initialized MP3DecInfo structure
 *
 * Outputs:     none
 *
 * Return:      none
 *
 * Notes:       safe to call even if some buffers were not allocated (uses SAFE_FREE)
 **************************************************************************************/
void FreeBuffers(MP3DecInfo *mp3DecInfo)
{
	if (mp3DecInfo != &g_mp3_dec_info)
		return;

	ClearBuffer(&g_mp3_dec_info, sizeof(g_mp3_dec_info));
	ClearBuffer(&g_frame_header, sizeof(g_frame_header));
	ClearBuffer(&g_side_info, sizeof(g_side_info));
	ClearBuffer(&g_scale_factor_info, sizeof(g_scale_factor_info));
	ClearBuffer(&g_huffman_info, sizeof(g_huffman_info));
	ClearBuffer(&g_dequant_info, sizeof(g_dequant_info));
	ClearBuffer(&g_imdct_info, sizeof(g_imdct_info));
	ClearBuffer(&g_subband_info, sizeof(g_subband_info));
}
