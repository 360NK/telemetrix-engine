#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "../proto/gtfs-realtime.pb-c.h"

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *) userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not Enough Memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int main(void) {
    CURL *curl_handle;
    CURLcode res;

    const char* url = "https://opendata.hamilton.ca/GTFS-RT/GTFS_VehiclePositions.pb";

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        printf("Connecting to %s ...\n", url);
        res = curl_easy_perform(curl_handle);

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();

        if(res != CURLE_OK) {
            fprintf(stderr, "Download Failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            return 1;
        }
        printf("SUCCESS: Downloaded %lu bytes of binary data.\n", (unsigned long)chunk.size);
        
        TransitRealtime__FeedMessage *msg;
        msg = transit_realtime__feed_message__unpack(NULL, chunk.size, (const uint8_t *)chunk.memory);

        if (msg == NULL) {
            fprintf(stderr, "Error unpacking incoming message\n");
            free(chunk.memory);
            return 1;
        }

        printf("Protobuf Decode Success!\n");

        if (msg->header) {
            printf("GTFS Version: %s\n", msg->header->gtfs_realtime_version);
        }
        printf("%-10s | %-10s | %-10s | %-20s\n", "FLEET NUM", "INT. ID", "ROUTE", "LOCATION");
        printf("-------------------------------------------------------------\n");

        for (size_t i = 0; i < msg->n_entity; i++) {
            TransitRealtime__FeedEntity *entity = msg->entity[i];

            if (entity->vehicle) {
                char *fleet_number = "[N/A]";
                if (entity->vehicle->vehicle && entity->vehicle->vehicle->label) {
                    fleet_number = entity->vehicle->vehicle->label;
                } else if (entity->id) {
                    fleet_number = entity->id;
                }

                char *internal_id = "[N/A]";
                if (entity->vehicle->vehicle && entity->vehicle->vehicle->id) {
                    internal_id = entity->vehicle->vehicle->id;
                }

                char *route_id = "[N/A]";
                if (entity->vehicle->trip && entity->vehicle->trip->route_id) {
                    route_id = entity->vehicle->trip->route_id;
                }

                float lat = 0.0, lon = 0.0;
                if (entity->vehicle->position) {
                    lat = entity->vehicle->position->latitude;
                    lon = entity->vehicle->position->longitude;
                }

                printf("%-10s | %-10s | %-10s | %.5f, %.5f\n", 
                    fleet_number, internal_id, route_id, lat, lon);
            }
        }

        transit_realtime__feed_message__free_unpacked(msg, NULL);

        free(chunk.memory);
    }
    return 0;
}
