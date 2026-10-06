/* ARM instruction witnesses: only external branch displacements vary. */
static const UiCodeWitness witnesses[] = {
    {UI_DISPLAY_CONTROL, 40, 0x3e0f9fdcU, {0xe59f1020U, 0xe52de004U, 0xe24dd02cU, 0xe58d000cU}},
    {UI_DISPLAY_START, 92, 0x78dc85e4U, {0xe92d4010U, 0xe5903058U, 0xe24dd010U, 0xe3530000U}},
    {UI_DISPLAY_STOP, 92, 0xfc192db2U, {0xe92d4070U, 0xe5903058U, 0xe1a04000U, 0xe3530000U}},
    {UI_DISPLAY_HISR_CREATE,
     120,
     0xd273b986U,
     {0xe92d4010U, 0xe59f406cU, 0xe1a0c000U, 0xe24dd010U}},
    {UI_BUSY_STOP, 232, 0xc3d1832aU, {0xe92d41f0U, 0xe59f50dcU, 0xe24dd008U, 0xe5950004U}},
    {UI_BUSY_START, 80, 0x9bda4a0dU, {0xe92d4010U, 0xe59f4044U, 0xe5940004U, 0xe3500000U}},
    {UI_CURSOR_HIDE, 80, 0x0d904e30U, {0xe92d4010U, 0xe59f4044U, 0xe59f3044U, 0xe594001cU}},
    {UI_CURSOR_SHOW, 508, 0x2bad1da0U, {0xe92d41f0U, 0xe59f41f0U, 0xe59f31f0U, 0xe594101cU}},
    {UI_APD_ALLOW, 188, 0x80faee43U, {0xe92d4030U, 0xe59f40b0U, 0xe24dd014U, 0xe5943000U}},
};
