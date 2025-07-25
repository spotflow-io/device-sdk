/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPOTFLOW_CERTS_H
#define SPOTFLOW_CERTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* This byte array can be generated by
 * "cat ca.crt | sed -e '1d;$d' | base64 -d |xxd -i"
 */

unsigned char spotflow_isrgrootx1_der[] = {
	0x30, 0x82, 0x05, 0x6b, 0x30, 0x82, 0x03, 0x53, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x11,
	0x00, 0x82, 0x10, 0xcf, 0xb0, 0xd2, 0x40, 0xe3, 0x59, 0x44, 0x63, 0xe0, 0xbb, 0x63, 0x82,
	0x8b, 0x00, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b,
	0x05, 0x00, 0x30, 0x4f, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02,
	0x55, 0x53, 0x31, 0x29, 0x30, 0x27, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x20, 0x49, 0x6e,
	0x74, 0x65, 0x72, 0x6e, 0x65, 0x74, 0x20, 0x53, 0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79,
	0x20, 0x52, 0x65, 0x73, 0x65, 0x61, 0x72, 0x63, 0x68, 0x20, 0x47, 0x72, 0x6f, 0x75, 0x70,
	0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0c, 0x49, 0x53, 0x52, 0x47,
	0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x58, 0x31, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x35, 0x30,
	0x36, 0x30, 0x34, 0x31, 0x31, 0x30, 0x34, 0x33, 0x38, 0x5a, 0x17, 0x0d, 0x33, 0x35, 0x30,
	0x36, 0x30, 0x34, 0x31, 0x31, 0x30, 0x34, 0x33, 0x38, 0x5a, 0x30, 0x4f, 0x31, 0x0b, 0x30,
	0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x29, 0x30, 0x27, 0x06,
	0x03, 0x55, 0x04, 0x0a, 0x13, 0x20, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x6e, 0x65, 0x74, 0x20,
	0x53, 0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x20, 0x52, 0x65, 0x73, 0x65, 0x61, 0x72,
	0x63, 0x68, 0x20, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55,
	0x04, 0x03, 0x13, 0x0c, 0x49, 0x53, 0x52, 0x47, 0x20, 0x52, 0x6f, 0x6f, 0x74, 0x20, 0x58,
	0x31, 0x30, 0x82, 0x02, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x02, 0x0f, 0x00, 0x30, 0x82, 0x02, 0x0a, 0x02,
	0x82, 0x02, 0x01, 0x00, 0xad, 0xe8, 0x24, 0x73, 0xf4, 0x14, 0x37, 0xf3, 0x9b, 0x9e, 0x2b,
	0x57, 0x28, 0x1c, 0x87, 0xbe, 0xdc, 0xb7, 0xdf, 0x38, 0x90, 0x8c, 0x6e, 0x3c, 0xe6, 0x57,
	0xa0, 0x78, 0xf7, 0x75, 0xc2, 0xa2, 0xfe, 0xf5, 0x6a, 0x6e, 0xf6, 0x00, 0x4f, 0x28, 0xdb,
	0xde, 0x68, 0x86, 0x6c, 0x44, 0x93, 0xb6, 0xb1, 0x63, 0xfd, 0x14, 0x12, 0x6b, 0xbf, 0x1f,
	0xd2, 0xea, 0x31, 0x9b, 0x21, 0x7e, 0xd1, 0x33, 0x3c, 0xba, 0x48, 0xf5, 0xdd, 0x79, 0xdf,
	0xb3, 0xb8, 0xff, 0x12, 0xf1, 0x21, 0x9a, 0x4b, 0xc1, 0x8a, 0x86, 0x71, 0x69, 0x4a, 0x66,
	0x66, 0x6c, 0x8f, 0x7e, 0x3c, 0x70, 0xbf, 0xad, 0x29, 0x22, 0x06, 0xf3, 0xe4, 0xc0, 0xe6,
	0x80, 0xae, 0xe2, 0x4b, 0x8f, 0xb7, 0x99, 0x7e, 0x94, 0x03, 0x9f, 0xd3, 0x47, 0x97, 0x7c,
	0x99, 0x48, 0x23, 0x53, 0xe8, 0x38, 0xae, 0x4f, 0x0a, 0x6f, 0x83, 0x2e, 0xd1, 0x49, 0x57,
	0x8c, 0x80, 0x74, 0xb6, 0xda, 0x2f, 0xd0, 0x38, 0x8d, 0x7b, 0x03, 0x70, 0x21, 0x1b, 0x75,
	0xf2, 0x30, 0x3c, 0xfa, 0x8f, 0xae, 0xdd, 0xda, 0x63, 0xab, 0xeb, 0x16, 0x4f, 0xc2, 0x8e,
	0x11, 0x4b, 0x7e, 0xcf, 0x0b, 0xe8, 0xff, 0xb5, 0x77, 0x2e, 0xf4, 0xb2, 0x7b, 0x4a, 0xe0,
	0x4c, 0x12, 0x25, 0x0c, 0x70, 0x8d, 0x03, 0x29, 0xa0, 0xe1, 0x53, 0x24, 0xec, 0x13, 0xd9,
	0xee, 0x19, 0xbf, 0x10, 0xb3, 0x4a, 0x8c, 0x3f, 0x89, 0xa3, 0x61, 0x51, 0xde, 0xac, 0x87,
	0x07, 0x94, 0xf4, 0x63, 0x71, 0xec, 0x2e, 0xe2, 0x6f, 0x5b, 0x98, 0x81, 0xe1, 0x89, 0x5c,
	0x34, 0x79, 0x6c, 0x76, 0xef, 0x3b, 0x90, 0x62, 0x79, 0xe6, 0xdb, 0xa4, 0x9a, 0x2f, 0x26,
	0xc5, 0xd0, 0x10, 0xe1, 0x0e, 0xde, 0xd9, 0x10, 0x8e, 0x16, 0xfb, 0xb7, 0xf7, 0xa8, 0xf7,
	0xc7, 0xe5, 0x02, 0x07, 0x98, 0x8f, 0x36, 0x08, 0x95, 0xe7, 0xe2, 0x37, 0x96, 0x0d, 0x36,
	0x75, 0x9e, 0xfb, 0x0e, 0x72, 0xb1, 0x1d, 0x9b, 0xbc, 0x03, 0xf9, 0x49, 0x05, 0xd8, 0x81,
	0xdd, 0x05, 0xb4, 0x2a, 0xd6, 0x41, 0xe9, 0xac, 0x01, 0x76, 0x95, 0x0a, 0x0f, 0xd8, 0xdf,
	0xd5, 0xbd, 0x12, 0x1f, 0x35, 0x2f, 0x28, 0x17, 0x6c, 0xd2, 0x98, 0xc1, 0xa8, 0x09, 0x64,
	0x77, 0x6e, 0x47, 0x37, 0xba, 0xce, 0xac, 0x59, 0x5e, 0x68, 0x9d, 0x7f, 0x72, 0xd6, 0x89,
	0xc5, 0x06, 0x41, 0x29, 0x3e, 0x59, 0x3e, 0xdd, 0x26, 0xf5, 0x24, 0xc9, 0x11, 0xa7, 0x5a,
	0xa3, 0x4c, 0x40, 0x1f, 0x46, 0xa1, 0x99, 0xb5, 0xa7, 0x3a, 0x51, 0x6e, 0x86, 0x3b, 0x9e,
	0x7d, 0x72, 0xa7, 0x12, 0x05, 0x78, 0x59, 0xed, 0x3e, 0x51, 0x78, 0x15, 0x0b, 0x03, 0x8f,
	0x8d, 0xd0, 0x2f, 0x05, 0xb2, 0x3e, 0x7b, 0x4a, 0x1c, 0x4b, 0x73, 0x05, 0x12, 0xfc, 0xc6,
	0xea, 0xe0, 0x50, 0x13, 0x7c, 0x43, 0x93, 0x74, 0xb3, 0xca, 0x74, 0xe7, 0x8e, 0x1f, 0x01,
	0x08, 0xd0, 0x30, 0xd4, 0x5b, 0x71, 0x36, 0xb4, 0x07, 0xba, 0xc1, 0x30, 0x30, 0x5c, 0x48,
	0xb7, 0x82, 0x3b, 0x98, 0xa6, 0x7d, 0x60, 0x8a, 0xa2, 0xa3, 0x29, 0x82, 0xcc, 0xba, 0xbd,
	0x83, 0x04, 0x1b, 0xa2, 0x83, 0x03, 0x41, 0xa1, 0xd6, 0x05, 0xf1, 0x1b, 0xc2, 0xb6, 0xf0,
	0xa8, 0x7c, 0x86, 0x3b, 0x46, 0xa8, 0x48, 0x2a, 0x88, 0xdc, 0x76, 0x9a, 0x76, 0xbf, 0x1f,
	0x6a, 0xa5, 0x3d, 0x19, 0x8f, 0xeb, 0x38, 0xf3, 0x64, 0xde, 0xc8, 0x2b, 0x0d, 0x0a, 0x28,
	0xff, 0xf7, 0xdb, 0xe2, 0x15, 0x42, 0xd4, 0x22, 0xd0, 0x27, 0x5d, 0xe1, 0x79, 0xfe, 0x18,
	0xe7, 0x70, 0x88, 0xad, 0x4e, 0xe6, 0xd9, 0x8b, 0x3a, 0xc6, 0xdd, 0x27, 0x51, 0x6e, 0xff,
	0xbc, 0x64, 0xf5, 0x33, 0x43, 0x4f, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x42, 0x30, 0x40,
	0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01, 0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x01,
	0x06, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03,
	0x01, 0x01, 0xff, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0x79,
	0xb4, 0x59, 0xe6, 0x7b, 0xb6, 0xe5, 0xe4, 0x01, 0x73, 0x80, 0x08, 0x88, 0xc8, 0x1a, 0x58,
	0xf6, 0xe9, 0x9b, 0x6e, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
	0x01, 0x0b, 0x05, 0x00, 0x03, 0x82, 0x02, 0x01, 0x00, 0x55, 0x1f, 0x58, 0xa9, 0xbc, 0xb2,
	0xa8, 0x50, 0xd0, 0x0c, 0xb1, 0xd8, 0x1a, 0x69, 0x20, 0x27, 0x29, 0x08, 0xac, 0x61, 0x75,
	0x5c, 0x8a, 0x6e, 0xf8, 0x82, 0xe5, 0x69, 0x2f, 0xd5, 0xf6, 0x56, 0x4b, 0xb9, 0xb8, 0x73,
	0x10, 0x59, 0xd3, 0x21, 0x97, 0x7e, 0xe7, 0x4c, 0x71, 0xfb, 0xb2, 0xd2, 0x60, 0xad, 0x39,
	0xa8, 0x0b, 0xea, 0x17, 0x21, 0x56, 0x85, 0xf1, 0x50, 0x0e, 0x59, 0xeb, 0xce, 0xe0, 0x59,
	0xe9, 0xba, 0xc9, 0x15, 0xef, 0x86, 0x9d, 0x8f, 0x84, 0x80, 0xf6, 0xe4, 0xe9, 0x91, 0x90,
	0xdc, 0x17, 0x9b, 0x62, 0x1b, 0x45, 0xf0, 0x66, 0x95, 0xd2, 0x7c, 0x6f, 0xc2, 0xea, 0x3b,
	0xef, 0x1f, 0xcf, 0xcb, 0xd6, 0xae, 0x27, 0xf1, 0xa9, 0xb0, 0xc8, 0xae, 0xfd, 0x7d, 0x7e,
	0x9a, 0xfa, 0x22, 0x04, 0xeb, 0xff, 0xd9, 0x7f, 0xea, 0x91, 0x2b, 0x22, 0xb1, 0x17, 0x0e,
	0x8f, 0xf2, 0x8a, 0x34, 0x5b, 0x58, 0xd8, 0xfc, 0x01, 0xc9, 0x54, 0xb9, 0xb8, 0x26, 0xcc,
	0x8a, 0x88, 0x33, 0x89, 0x4c, 0x2d, 0x84, 0x3c, 0x82, 0xdf, 0xee, 0x96, 0x57, 0x05, 0xba,
	0x2c, 0xbb, 0xf7, 0xc4, 0xb7, 0xc7, 0x4e, 0x3b, 0x82, 0xbe, 0x31, 0xc8, 0x22, 0x73, 0x73,
	0x92, 0xd1, 0xc2, 0x80, 0xa4, 0x39, 0x39, 0x10, 0x33, 0x23, 0x82, 0x4c, 0x3c, 0x9f, 0x86,
	0xb2, 0x55, 0x98, 0x1d, 0xbe, 0x29, 0x86, 0x8c, 0x22, 0x9b, 0x9e, 0xe2, 0x6b, 0x3b, 0x57,
	0x3a, 0x82, 0x70, 0x4d, 0xdc, 0x09, 0xc7, 0x89, 0xcb, 0x0a, 0x07, 0x4d, 0x6c, 0xe8, 0x5d,
	0x8e, 0xc9, 0xef, 0xce, 0xab, 0xc7, 0xbb, 0xb5, 0x2b, 0x4e, 0x45, 0xd6, 0x4a, 0xd0, 0x26,
	0xcc, 0xe5, 0x72, 0xca, 0x08, 0x6a, 0xa5, 0x95, 0xe3, 0x15, 0xa1, 0xf7, 0xa4, 0xed, 0xc9,
	0x2c, 0x5f, 0xa5, 0xfb, 0xff, 0xac, 0x28, 0x02, 0x2e, 0xbe, 0xd7, 0x7b, 0xbb, 0xe3, 0x71,
	0x7b, 0x90, 0x16, 0xd3, 0x07, 0x5e, 0x46, 0x53, 0x7c, 0x37, 0x07, 0x42, 0x8c, 0xd3, 0xc4,
	0x96, 0x9c, 0xd5, 0x99, 0xb5, 0x2a, 0xe0, 0x95, 0x1a, 0x80, 0x48, 0xae, 0x4c, 0x39, 0x07,
	0xce, 0xcc, 0x47, 0xa4, 0x52, 0x95, 0x2b, 0xba, 0xb8, 0xfb, 0xad, 0xd2, 0x33, 0x53, 0x7d,
	0xe5, 0x1d, 0x4d, 0x6d, 0xd5, 0xa1, 0xb1, 0xc7, 0x42, 0x6f, 0xe6, 0x40, 0x27, 0x35, 0x5c,
	0xa3, 0x28, 0xb7, 0x07, 0x8d, 0xe7, 0x8d, 0x33, 0x90, 0xe7, 0x23, 0x9f, 0xfb, 0x50, 0x9c,
	0x79, 0x6c, 0x46, 0xd5, 0xb4, 0x15, 0xb3, 0x96, 0x6e, 0x7e, 0x9b, 0x0c, 0x96, 0x3a, 0xb8,
	0x52, 0x2d, 0x3f, 0xd6, 0x5b, 0xe1, 0xfb, 0x08, 0xc2, 0x84, 0xfe, 0x24, 0xa8, 0xa3, 0x89,
	0xda, 0xac, 0x6a, 0xe1, 0x18, 0x2a, 0xb1, 0xa8, 0x43, 0x61, 0x5b, 0xd3, 0x1f, 0xdc, 0x3b,
	0x8d, 0x76, 0xf2, 0x2d, 0xe8, 0x8d, 0x75, 0xdf, 0x17, 0x33, 0x6c, 0x3d, 0x53, 0xfb, 0x7b,
	0xcb, 0x41, 0x5f, 0xff, 0xdc, 0xa2, 0xd0, 0x61, 0x38, 0xe1, 0x96, 0xb8, 0xac, 0x5d, 0x8b,
	0x37, 0xd7, 0x75, 0xd5, 0x33, 0xc0, 0x99, 0x11, 0xae, 0x9d, 0x41, 0xc1, 0x72, 0x75, 0x84,
	0xbe, 0x02, 0x41, 0x42, 0x5f, 0x67, 0x24, 0x48, 0x94, 0xd1, 0x9b, 0x27, 0xbe, 0x07, 0x3f,
	0xb9, 0xb8, 0x4f, 0x81, 0x74, 0x51, 0xe1, 0x7a, 0xb7, 0xed, 0x9d, 0x23, 0xe2, 0xbe, 0xe0,
	0xd5, 0x28, 0x04, 0x13, 0x3c, 0x31, 0x03, 0x9e, 0xdd, 0x7a, 0x6c, 0x8f, 0xc6, 0x07, 0x18,
	0xc6, 0x7f, 0xde, 0x47, 0x8e, 0x3f, 0x28, 0x9e, 0x04, 0x06, 0xcf, 0xa5, 0x54, 0x34, 0x77,
	0xbd, 0xec, 0x89, 0x9b, 0xe9, 0x17, 0x43, 0xdf, 0x5b, 0xdb, 0x5f, 0xfe, 0x8e, 0x1e, 0x57,
	0xa2, 0xcd, 0x40, 0x9d, 0x7e, 0x62, 0x22, 0xda, 0xde, 0x18, 0x27
};

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CERTS_H */
