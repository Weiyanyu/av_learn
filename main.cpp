
#if defined(__cplusplus)
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#if defined(__cplusplus)
}
#endif

#include <string>
#include <iostream>
#include <fstream>
#include "device.h"

void initParam()
{
    avdevice_register_all();    
    av_log_set_level(AV_LOG_DEBUG);
}

int main()
{
    initParam();

    // create a device
    Device device("hw:0");

    // start record and save output file
    device.record("out.pcm");
    
    return 0;
}