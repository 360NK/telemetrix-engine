#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sqlite3.h>

#include <unistd.h>
#include <signal.h>

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

int init_db(sqlite3 *db) {
    char *err_msg = 0;
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS vehicle_positions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "fleet_number TEXT, "
        "internal_id TEXT, "
        "route_id TEXT, "
        "lat REAL, "
        "lon REAL, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE INDEX IF NOT EXISTS idx_route ON vehicle_positions(route_id);";
    
    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return 1;
    }

    return 0;
}

void save_vehicle(sqlite3 *db, const char *fleet, const char *internal, const char *route, float lat, float lon) {
    sqlite3_stmt *res;
    const char *sql = "INSERT INTO vehicle_positions (fleet_number, internal_id, route_id, lat, lon) VALUES (?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);

    if (rc == SQLITE_OK) {
        sqlite3_bind_text(res, 1, fleet, -1, SQLITE_STATIC);
        sqlite3_bind_text(res, 2, internal, -1, SQLITE_STATIC);
        sqlite3_bind_text(res, 3, route, -1, SQLITE_STATIC);
        sqlite3_bind_double(res, 4, (double)lat);
        sqlite3_bind_double(res, 5, (double)lon);
        
        sqlite3_step(res);
    } else {
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(res);
}

static volatile int keep_running = 1;

void handle_sigint(int sig) {
    printf("\nCaught signal %d. Shutting down safely...\n", sig);
    keep_running = 0;
}

int main(void) {
    sqlite3 *db;
    if (sqlite3_open("transit.db", &db)) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    if (init_db(db) != 0) {
        sqlite3_close(db);
        return 1;
    }
    
    curl_global_init(CURL_GLOBAL_ALL);
    signal(SIGINT, handle_sigint);

    const char* url = "https://opendata.hamilton.ca/GTFS-RT/GTFS_VehiclePositions.pb";
    printf("Telemetrix Engine Started. Press Ctrl+C to stop.\n");

    while (keep_running) {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;

        CURL *curl_handle = curl_easy_init();
        CURLcode res;

        if (curl_handle) {
            curl_easy_setopt(curl_handle, CURLOPT_URL, url);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    
            printf("Connecting to %s ...\n", url);
            res = curl_easy_perform(curl_handle);
    
            curl_easy_cleanup(curl_handle);
    
            if(res != CURLE_OK) {
                fprintf(stderr, "Download Failed: %s\n", curl_easy_strerror(res));
                free(chunk.memory);
                sleep(10);
                continue;
            }
            printf("SUCCESS: Downloaded %lu bytes of binary data.\n", (unsigned long)chunk.size);
            
            TransitRealtime__FeedMessage *msg;
            msg = transit_realtime__feed_message__unpack(NULL, chunk.size, (const uint8_t *)chunk.memory);
    
            if (msg == NULL) {
                fprintf(stderr, "Error unpacking incoming message\n");
                free(chunk.memory);
                sleep(10);
                continue;
            }
    
            sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
            int saved_count = 0;
            for (size_t i = 0; i < msg->n_entity; i++) {
                TransitRealtime__FeedEntity *entity = msg->entity[i];
                if (entity->vehicle) {
                    char *fleet = (entity->vehicle->vehicle && entity->vehicle->vehicle->label) ? 
                                   entity->vehicle->vehicle->label : (entity->id ? entity->id : "UNKOWN");
                    
                    char *internal = (entity->vehicle->vehicle && entity->vehicle->vehicle->id) ? 
                                      entity->vehicle->vehicle->id : "UNKOWN";
                    
                    char *route = (entity->vehicle->trip && entity->vehicle->trip->route_id) ? 
                                   entity->vehicle->trip->route_id : "UNKOWN";
    
                    float lat = (entity->vehicle->position) ? entity->vehicle->position->latitude : 0.0;
                    float lon = (entity->vehicle->position) ? entity->vehicle->position->longitude : 0.0;
    
                    save_vehicle(db, fleet, internal, route, lat, lon);
                    saved_count++;
                }
            }
    
            sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
            printf("DONE: Saved %d vehicle records to database.\n", saved_count);
    
            transit_realtime__feed_message__free_unpacked(msg, NULL);
            free(chunk.memory);
        }
        printf("Sleeping for 10s...\n");
        sleep(10);
    }
    printf("Exiting...\n");
    sqlite3_close(db);
    curl_global_cleanup();
    
    return 0;
}
