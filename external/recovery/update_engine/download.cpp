/*************************************************************************
    > File Name: download.cpp
    > Author: jkand.huang
    > Mail: jkand.huang@rock-chips.com
    > Created Time: Thu 07 Mar 2019 10:30:31 AM CST
 ************************************************************************/

#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include "log.h"

size_t my_write_func(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return fwrite(ptr, size, nmemb, stream);
}

extern double processvalue;
static int down_processvalue;

int my_progress_func(char *progress_data,
                     double t, /* dltotal */
                     double d, /* dlnow */
                     double ultotal,
                     double ulnow)
{
    processvalue = ulnow / ultotal * 100 / 110;
    //LOGI("ultotal is %f, ulnow is %f, t is %f, d is %f\n", ultotal, ulnow, t, d);
    if (down_processvalue != (int)(d / t * 100)) {
        down_processvalue = (int)(d / t * 100);
        LOGI("down_processvalue is %d%\n", down_processvalue);
    }

    return 0;
}

int download_file(char *url, char const *output_filename)
{
    if (strncmp(url, "http", 4) != 0) {
        LOGI("where the file is local.\n");
        return -2;
    }
    CURL *curl;
    CURLcode res;
    FILE *outfile;
    char const *progress_data = "* ";
    down_processvalue = -1;

    curl = curl_easy_init();
    if(curl)
    {
        outfile = fopen(output_filename, "wb");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, outfile);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, my_write_func);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, false);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, my_progress_func);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, progress_data);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
            LOGE("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        fclose(outfile);
        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    if(res != CURLE_OK){
        LOGE("download Error.\n");
        return -1;
    }
    return 0;
}

