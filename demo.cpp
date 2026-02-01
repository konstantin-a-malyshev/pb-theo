#include "inkview.h"
#include "sqlite3.h"
#include "curl/curl.h"
#include <math.h>
#include <string>
#include <limits>
#include "json-c/json.h"
#include "theo_server.h"


// e:\system\profiles\default\config\books.db

#define DB_FILE USERDATA TEMPDIR "/demo07.sqlite3"

#define BOOKS_DB_FILE "/mnt/ext1/system/profiles/default/config/books.db"

static ifont *font;
static const int kFontSize = 16;
static int y_log;

static int last_imported_index = std::numeric_limits<int>::max();

static void log_message(const char *msg)
{
	if (strlen(msg) == 0) {
		return;
	}
	DrawTextRect(0, y_log, ScreenWidth(), kFontSize, msg, ALIGN_LEFT);
	PartialUpdate(0, y_log, ScreenWidth(), y_log + kFontSize + 2);
	y_log += kFontSize + 10;
}

static void check_internet(void)
{
    log_message("Checking internet connectivity...");
	char buffer[2048];

	const char *url = "http://checkip.amazonaws.com/";
	int retsize;
	char *cookie = NULL;
	char *post = NULL;

	snprintf(buffer, sizeof(buffer), "HTTP Request to %s", url);
	log_message(buffer);

	void *result = QuickDownloadExt(url, &retsize, 15, cookie, post);

	snprintf(buffer, sizeof(buffer), "Response size: %d", retsize);
	log_message(buffer);

	log_message("Response content:");
	snprintf(buffer, sizeof(buffer), "Response content: %.1024s", (char *)result);
	log_message(buffer);

	free(result);
    log_message("Internet connectivity checked.");
}

static size_t theo_server_health_check_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	snprintf(buffer, sizeof(buffer), "  Data %d bytes : %.128s", data_size, ptr);
	log_message(buffer);

	// Even if we didn't display everything, we signal the system we used all received data
	return data_size;
}

static void theo_server_health_check()
{
	char buffer[2048];

	log_message("Checking Theo Server health...");

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_message("Failed initializing curl");
		return;
	}

    const char *url = THEO_SERVER_API_URL "/healthz";

    log_message("Health check URL:");
    log_message(url);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, theo_server_health_check_callback);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		snprintf(buffer, sizeof(buffer), "Error %d : %s", res, curl_easy_strerror(res));
		log_message(buffer);

		goto end;
	}

	end:
	curl_easy_cleanup(curl);

	log_message("Theo server health checked.");
}

static size_t theo_server_get_max_import_index_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	snprintf(buffer, sizeof(buffer), "%s", ptr);
    log_message("Content received:");
	log_message(buffer);

	json_object *root = json_tokener_parse(buffer);

	if (json_object_get_type(root) != json_type_object) {
        log_message("Error: root is not an object!");
        json_object_put(root);  // free root
    } else {
        struct json_object *max_id_obj = NULL;
        if (!json_object_object_get_ex(root, "maxImportIndex", &max_id_obj)) {
            log_message("Error: `maxImportIndex` not found!");
            json_object_put(root);  // free root
        } else {
            // Ensure it is an integer and read it
            if (json_object_get_type(max_id_obj) != json_type_int) {
                log_message("Error: `maxImportIndex` is not an integer!");
                json_object_put(root);
            } else {
                last_imported_index = json_object_get_int(max_id_obj);
                log_message("Max Import Index:");
                log_message(std::to_string(last_imported_index).c_str());
                json_object_put(root);
            }
        }
	}

	// Even if we didn't display everything, we signal the system we used all received data
	return data_size;
}

static void theo_server_get_max_import_index()
{
	char buffer[2048];

	log_message("Getting Theo Server max import index...");

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_message("Failed initializing curl");
		return;
	}

    const char *url = THEO_SERVER_API_URL "/v1/quotations/import-index/max";
    log_message("Theo Server get max import index URL:");
    log_message(url);

    // Build header list
    struct curl_slist* headers = nullptr;
    const char *api_key_header = "X-API-Key: " THEO_SERVER_API_KEY;
    headers = curl_slist_append(headers, api_key_header);
    log_message("Using header:");
    log_message(api_key_header);

	curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, theo_server_get_max_import_index_callback);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		snprintf(buffer, sizeof(buffer), "Error %d : %s", res, curl_easy_strerror(res));
		log_message(buffer);
	} else {
    	log_message("Theo Server max index received.");
    }
    curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}


static void json_01()
{
	char buffer[2048];

	const char *json_string = "{\"a_string\":\"plop!\",\"my_int\":123}";
	log_message(json_string);

	json_object *obj = json_tokener_parse(json_string);

	snprintf(buffer, sizeof(buffer), "type=%d (%s)", json_object_get_type(obj), json_type_to_name(json_object_get_type(obj)));
	log_message(buffer);

	if (json_object_get_type(obj) == json_type_object) {
		json_object_object_foreach(obj, key, val) {
			json_type type = json_object_get_type(val);
			if (type == json_type_int) {
				snprintf(buffer, sizeof(buffer), "  > %s :: type=%d (%s) -> %d", key, type, json_type_to_name(type), json_object_get_int(val));
			}
			else if (type == json_type_string) {
				snprintf(buffer, sizeof(buffer), "  > %s :: type=%d (%s) -> %s", key, type, json_type_to_name(type), json_object_get_string(val));
			}
			// ... here, deal with the other types of data ; including array and object (recursive ;-) )
			log_message(buffer);
		}
	}
}

// Serialize some C/C++ data structures to JSON
static void json_02()
{
	log_message("Serializing C/C++ data structures to JSON...");

	const char *json_string;
	json_object *obj, *sub_obj1;

	// Empty object: {}
	obj = json_object_new_object();
	json_string = json_object_to_json_string(obj);
	free(obj);
	log_message(json_string);
	free((void *)json_string);

	// Very simple object: {"my_int":123,"a_string":"Plop!"}
	obj = json_object_new_object();
	json_object_object_add(obj, "my_int", json_object_new_int(123));
	json_object_object_add(obj, "a_string", json_object_new_string("Plop!"));
	json_string = json_object_to_json_string(obj);
	free(obj);
	log_message(json_string);
	free((void *)json_string);

	// An array that contains an object and an integer: [{"key":"some val"},123456]
	obj = json_object_new_array();
	sub_obj1 = json_object_new_object();
	json_object_object_add(sub_obj1, "key", json_object_new_string("some val"));
	json_object_array_add(obj, sub_obj1);
	json_object_array_add(obj, json_object_new_int(123456));
	json_string = json_object_to_json_string(obj);
	free(obj);
	log_message(json_string);
	free((void *)json_string);
}

static size_t curl_03_header_callback(char *ptr, size_t size, size_t nitems, void *userdata)
{
	char buffer[1024];

	int data_size = size * nitems;
	snprintf(buffer, sizeof(buffer), "  Header : %d bytes : %.128s", data_size, ptr);
	log_message(buffer);

	return data_size;
}

static size_t curl_03_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	snprintf(buffer, sizeof(buffer), "  Data %d bytes : %.128s", data_size, ptr);
	log_message(buffer);

	// Even if we didn't display everything, we signal the system we used all received data
	return data_size;
}

static void http_request_03()
{
	char buffer[2048];

	log_message("Start 3rd try (curl).");

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_message("Failed initializing curl");
		return;
	}

	const char *url = "http://checkip.amazonaws.com/";
	//const char *url = "https://blog.pascal-martin.fr/post/directives-ini-c-est-le-mal.html";
	//const char *url = "http://bit.ly/2bPtNry";

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_03_header_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_03_write_callback);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		snprintf(buffer, sizeof(buffer), "Error %d : %s", res, curl_easy_strerror(res));
		log_message(buffer);

		goto end;
	}

	end:
	curl_easy_cleanup(curl);

	log_message("End 3rd try (curl).");
}

static int read_quotations_callback(void *not_used, int argc, char **argv, char **col_name){
	char buffer[128*5];
	char row_buffer[128];

    char *oid  = argv[0];
    char *json = argv[1];
    char *book = argv[2];

    log_message(oid);
    log_message(json);
    log_message(book);

	return 0;
}

static void read_quotations() {
	char buffer[2048];

	sqlite3 *db;
	int result;
	char *err_msg;

	result = sqlite3_open(BOOKS_DB_FILE, &db);
	if (result) {
		snprintf(buffer, sizeof(buffer), "Fail opening DB: %s", sqlite3_errmsg(db));
		log_message(buffer);
		goto exit;
	}

    char query[512];
    snprintf(query, sizeof query,
        "SELECT t.OID, t.Val, b.Title, b.Authors\n"
        "FROM Tags t\n"
        "JOIN Items i ON i.OID = t.ItemID\n"
        "LEFT JOIN Books b ON b.OID = i.ParentID\n"
        "WHERE t.TagID = 104 AND t.OID > %d\n"
        "LIMIT 5\n"
        "ORDER BY t.oid DESC",
        last_imported_index);

	log_message("Selecting quotations...");
    log_message(query);

	result = sqlite3_exec(db, query, read_quotations_callback, 0, &err_msg);
	if (result != SQLITE_OK) {
		snprintf(buffer, sizeof(buffer), "Fail selecting : %s", err_msg);
		log_message(buffer);
		goto exit;
	}

	exit:
	log_message("All done.");
	sqlite3_close(db);
}

static int callback_01(void *not_used, int argc, char **argv, char **col_name){
	char buffer[128*5];
	char row_buffer[128];

	strcpy(buffer, " > ");
	for(int i=0; i<argc && i<5 ; i++) {
		snprintf(row_buffer, sizeof(row_buffer), "%s=%s, ", col_name[i], argv[i] ? argv[i] : "NULL");
		strcat(buffer, row_buffer);
	}

	if (strlen(buffer) > 3) {
		log_message(buffer);
	}
	return 0;
}

static void database_01()
{
	char buffer[2048];

	sqlite3 *db;
	int result;
	char *err_msg;

	// Ensure the DB doesn't already exist
	// iv_unlink(DB_FILE);

	result = sqlite3_open(BOOKS_DB_FILE, &db);
	if (result) {
		snprintf(buffer, sizeof(buffer), "Fail opening DB : %s", sqlite3_errmsg(db));
		log_message(buffer);
		goto exit;
	}

	log_message("Select...");
	result = sqlite3_exec(db, "select * from tags limit 10", callback_01, 0, &err_msg);
	if (result != SQLITE_OK) {
		snprintf(buffer, sizeof(buffer), "Fail selecting : %s", err_msg);
		log_message(buffer);
		goto exit;
	}

	exit:
	log_message("All done.");
	sqlite3_close(db);
}


static int main_handler(int event_type, int param_one, int param_two)
{
	int result = 0;

	static int step = 0;

	switch (event_type) {
	case EVT_INIT:
		font = OpenFont("LiberationSans", 16, 1);
		SetFont(font, BLACK);
		y_log = 0;
		ClearScreen();
		FullUpdate();
		break;
	case EVT_SHOW:

		break;
	case EVT_KEYPRESS:
		if (param_one == IV_KEY_PREV) {
			CloseApp();
			return 1;
		}
		else if (param_one == IV_KEY_NEXT) {
			//*
			if (step == 0) {
                check_internet();
                theo_server_health_check();
				// database_01();
                // http_request_03();
                // json_01();
                // json_02();
			} else if (step == 1) {
                theo_server_get_max_import_index();
            } else if (step == 2) {
                read_quotations();
            }
			else {
				CloseApp();
			}
			//*/

			step++;
			return 1;
		}

		break;
	case EVT_EXIT:
		CloseFont(font);
		break;
	default:
		break;
	}

    return result;
}


int main (int argc, char* argv[])
{
    InkViewMain(main_handler);

    return 0;
}