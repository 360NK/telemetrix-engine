#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sqlite3.h>

#include <unistd.h>
#include <signal.h>

#include <pthread.h>
#include "../include/buffer.h"

#include "../proto/gtfs-realtime.pb-c.h"

sqlite3 *db = NULL;
RingBuffer *engine_buffer = NULL;

void handle_sigint(int sig) {
    printf("\nCaught signal %d. Shutting down safely...\n", sig);
    if (engine_buffer != NULL) {
        buffer_signal_shutdown(engine_buffer);
    }
}

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

void *fetcher_thread(void *arg) {
    RingBuffer *rb = (RingBuffer *)arg;
    const char* url = "https://opendata.hamilton.ca/GTFS-RT/GTFS_VehiclePositions.pb";
    
    printf("[Fetcher] thread started. Ready to hit the network. \n");

    while (!rb->shutdown) {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;

        CURL *curl_handle = curl_easy_init();
        if (!curl_handle) {
            free(chunk.memory);
            sleep(10);
            continue;
        }

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        CURLcode res = curl_easy_perform(curl_handle);
        curl_easy_cleanup(curl_handle);

        if(res != CURLE_OK) {
            fprintf(stderr, "[Fetcher] Download Failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            sleep(10);
            continue;
        }

        TransitRealtime__FeedMessage *msg;
        msg = transit_realtime__feed_message__unpack(NULL, chunk.size, (const uint8_t *)chunk.memory);

        if (msg == NULL) {
            fprintf(stderr, "[Fetcher] Error unpacking incoming message\n");
            free(chunk.memory);
            sleep(10);
            continue;
        }

        int pushed_count = 0;
        for (size_t i = 0; i < msg->n_entity; i++) {
            TransitRealtime__FeedEntity *entity = msg->entity[i];

            if (entity->vehicle && entity->vehicle->position) {
                VehicleData item;

                const char *raw_fleet = (entity->vehicle->vehicle && entity->vehicle->vehicle->label) ? entity->vehicle->vehicle->label : (entity->id ? entity->id : "UNKNOWN");
                strncpy(item.fleet_number, raw_fleet, sizeof(item.fleet_number) - 1);
                item.fleet_number[sizeof(item.fleet_number) - 1] = '\0';

                const char *raw_internal_id = (entity->vehicle->vehicle && entity->vehicle->vehicle->id) ? entity->vehicle->vehicle->id : "UNKNOWN";
                strncpy(item.internal_id, raw_internal_id, sizeof(item.internal_id) - 1);
                item.internal_id[sizeof(item.internal_id) - 1] = '\0';

                const char *raw_route = (entity->vehicle->trip && entity->vehicle->trip->route_id) ? entity->vehicle->trip->route_id : "UNKNOWN";
                strncpy(item.route_id, raw_route, sizeof(item.route_id) - 1);
                item.route_id[sizeof(item.route_id) - 1] = '\0';

                item.lat = entity->vehicle->position->latitude;
                item.lon = entity->vehicle->position->longitude;

                if (buffer_push(rb, item)) {
                    pushed_count++;
                } else {
                    break; 
                }
            }
        }
        printf("[Fetcher] Successfully pushed %d vehicles to the Ring Buffer.\n", pushed_count);

        transit_realtime__feed_message__free_unpacked(msg, NULL);
        free(chunk.memory);

        for(int i = 0; i < 10 && !rb->shutdown; i++) {
            sleep(1);
        }
    }

    printf("[Fetcher] Shutting down cleanly.\n");
    return NULL;
}

void *logger_thread(void *arg) {
    RingBuffer *rb = (RingBuffer *)arg;
    VehicleData item;

    printf("[Logger] Thread started. Ready to write to SQLite.\n");

    int transaction_count = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    while (!rb->shutdown) {
        if (buffer_pop(rb, &item)) {
            save_vehicle(db, item.fleet_number, item.internal_id, item.route_id, item.lat, item.lon);
            transaction_count++;

            if (transaction_count >= 100) {
                sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
                printf("[Logger] Committed %d vehicles to disk.\n", transaction_count);
                transaction_count = 0;
                
                if (!rb->shutdown) {
                    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
                }
            }
        }
    }

    if (transaction_count > 0) {
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        printf("[Logger] Committed final %d vehicles before shutting down.\n", transaction_count);
    }

    printf("[Logger] Shutting down cleanly.\n");
    return NULL;
}

int main(void) {

    if (sqlite3_open("transit.db", &db)) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    if (init_db(db) != 0) {
        sqlite3_close(db);
        return 1;
    }
    
    curl_global_init(CURL_GLOBAL_ALL);
    
    engine_buffer = buffer_init(1000);
    if (!engine_buffer) {
        fprintf(stderr, "Fatal: Could not alocate Ring Buffer. \n");
        return 1;
    }

    signal(SIGINT, handle_sigint);

    printf("Telemetrix Engine Started. Press Ctrl+C to stop.\n");

    pthread_t fetcher_id, logger_id;

    printf("Telemetrix Engine Started. Spawning threads...\n");

    pthread_create(&fetcher_id, NULL, fetcher_thread, engine_buffer);
    pthread_create(&logger_id, NULL, logger_thread, engine_buffer);

    printf("Threads running. Press Ctrl+C to stop.\n");

    pthread_join(fetcher_id, NULL);
    pthread_join(logger_id, NULL);

    buffer_destroy(engine_buffer);
    sqlite3_close(db);
    curl_global_cleanup();
    
    return 0;
}
