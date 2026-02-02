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
static int quotations_sent = 0;
static int quotations_imported = 0;
static int quotations_conflicted = 0;
static int quotations_failed = 0;

static int last_imported_index = std::numeric_limits<int>::max();

static void log_message(const char *msg)
{
	if (strlen(msg) == 0) {
		return;
	}
	DrawTextRect(0, y_log, ScreenWidth(), kFontSize, msg, ALIGN_LEFT);
	PartialUpdate(0, y_log, ScreenWidth(), y_log + kFontSize + 10);
	y_log += kFontSize + 10;
}

static void log_message_no_newline(const char *msg)
{
	if (strlen(msg) == 0) {
		return;
	}
	FillArea(0, y_log, ScreenWidth(), kFontSize + 15, WHITE);
	DrawTextRect(0, y_log, ScreenWidth(), kFontSize, msg, ALIGN_LEFT);
	PartialUpdate(0, y_log, ScreenWidth(), y_log + kFontSize + 15);
}

static void check_internet(void)
{
    log_message("Checking internet connectivity...");
	char buffer[2048];

	const char *url = "http://checkip.amazonaws.com/";
	int retsize;
	char *cookie = NULL;
	char *post = NULL;

	// snprintf(buffer, sizeof(buffer), "HTTP Request to %s", url);
	// log_message(buffer);

	void *result = QuickDownloadExt(url, &retsize, 15, cookie, post);

	snprintf(buffer, sizeof(buffer), "My IP: %.1024s", (char *)result);
	log_message(buffer);

	free(result);
}

static size_t theo_server_health_check_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	snprintf(buffer, sizeof(buffer), "Theo server health: %.128s", ptr);
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
}

static size_t theo_server_get_max_import_index_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	snprintf(buffer, sizeof(buffer), "%s", ptr);

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
				snprintf(buffer, sizeof(buffer), "Last imported importIndex: %s", std::to_string(last_imported_index).c_str());
                log_message(buffer);
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

	log_message("Getting Theo Server last imported importIndex...");

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_message("Failed initializing curl");
		return;
	}

    const char *url = THEO_SERVER_API_URL "/v1/quotations/import-index/max";

    // Build header list
    struct curl_slist* headers = nullptr;
    const char *api_key_header = "X-API-Key: " THEO_SERVER_API_KEY;
    headers = curl_slist_append(headers, api_key_header);

	curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, theo_server_get_max_import_index_callback);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		snprintf(buffer, sizeof(buffer), "Error %d : %s", res, curl_easy_strerror(res));
		log_message(buffer);
	}

    curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

static size_t theo_server_post_quotation_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	// Warning: the received data it not NUL-terminated
	char buffer[1024];

	int data_size = size * nmemb;
	// snprintf(buffer, sizeof(buffer), "  Data %d bytes : %.128s", data_size, ptr);
	// log_message(buffer);

	// Even if we didn't display everything, we signal the system we used all received data
	return data_size;
}

static int theo_server_post_quotation(const char *quotation_json_string)
{
	char buffer[2048];

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_message("Failed initializing curl");
		return 1;
	}

	const char *url = THEO_SERVER_API_URL "/v1/quotations/import";

	// Build header list
	struct curl_slist* headers = nullptr;
	const char *api_key_header = "X-API-Key: " THEO_SERVER_API_KEY;
	const char *content_type_header = "Content-Type: application/json";
	headers = curl_slist_append(headers, api_key_header);
	headers = curl_slist_append(headers, content_type_header);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, quotation_json_string);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(quotation_json_string));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, theo_server_post_quotation_callback);

	CURLcode res = curl_easy_perform(curl);
	quotations_sent++;

	long response_code = 0;
	if (res == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		if(response_code == 201) {
			quotations_imported++;
		} else if (response_code == 409) {
			quotations_conflicted++;
		} else {
			quotations_failed++;
			snprintf(buffer, sizeof(buffer), "Failed sending quotation. HTTP Response code: %ld", response_code);
			log_message(buffer);
		}
	} else {
		snprintf(buffer, sizeof(buffer), "Error %d : %s", res, curl_easy_strerror(res));
		log_message(buffer);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return 1;
	}

	snprintf(buffer, sizeof(buffer), "Quotations sent: %d, imported: %d, conflicted: %d, failed: %d.", quotations_sent, quotations_imported, quotations_conflicted, quotations_failed);
	log_message_no_newline(buffer);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return 0;
}

static const char * json_get_string_property(struct json_object *obj, const char *property_name) {
	struct json_object *prop_obj = NULL;
	if (! json_object_object_get_ex(obj, property_name, &prop_obj)) {
		// log_message("Error: The property not found in the tag value!");
		return NULL;
	} else{
		if (! json_object_is_type(prop_obj, json_type_string)) {
			// log_message("Error: The property is not a string!");
			return NULL;
		} else {
			return json_object_get_string(prop_obj);
		}
	}
}

static int read_quotations_callback(void *not_used, int argc, char **argv, char **col_name){
    char buffer[2048];

	char *oid  = argv[0];
    char *json = argv[1];
    char *book = argv[2];

	struct json_object *root = json_tokener_parse(json);
    if (!root || !json_object_is_type(root, json_type_object)) {
		log_message(" ");
		log_message("Error: Tag Value parse error!");
	    json_object_put(root); // free
		return 1;
    } else {
		const char *text = json_get_string_property(root, "text");
		if (!text) {
			snprintf(buffer, sizeof(buffer), "Quotation %s has NULL text", oid);
			log_message(" ");
			log_message(buffer);
		    text = "null";
		}

		const char *begin = json_get_string_property(root, "begin");
		if (!begin) {
			snprintf(buffer, sizeof(buffer), "Quotation %s has NULL begin", oid);
			log_message(" ");
			log_message(buffer);
		    begin = "null";
		}

		const char *end = json_get_string_property(root, "end");
		if (!end) {
			snprintf(buffer, sizeof(buffer), "Quotation %s has NULL end", oid);
			log_message(" ");
			log_message(buffer);
		    end = "null";
		}

		std::string caption = std::string(book) + " (OID: " + std::string(oid) + ")";
		std::string position = std::string(begin) + " - " + std::string(end);

		// create JSON object to send to Theo Server
		json_object *quotation_obj = json_object_new_object();
		json_object_object_add(quotation_obj, "caption", json_object_new_string(caption.c_str()));
		json_object_object_add(quotation_obj, "text", json_object_new_string(text));
		json_object_object_add(quotation_obj, "book", json_object_new_string(book));
		json_object_object_add(quotation_obj, "position", json_object_new_string(position.c_str()));
		json_object_object_add(quotation_obj, "importIndex", json_object_new_int(atoi(oid)));
		json_object_object_add(quotation_obj, "status", json_object_new_string("new"));

		const char *quotation_json_string = json_object_to_json_string(quotation_obj);

		// send POST request to theo server to /v1/quotations/import
		theo_server_post_quotation(quotation_json_string);		

		json_object_put(quotation_obj); // free
	}

    json_object_put(root); // free

	return 0;
}

static void read_quotations() {
	log_message("Reading quotations from Books DB...");
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
        "ORDER BY t.oid ASC\n"
		// "LIMIT 1"
		";",
        last_imported_index);

	result = sqlite3_exec(db, query, read_quotations_callback, 0, &err_msg);
	if (result != SQLITE_OK) {
		snprintf(buffer, sizeof(buffer), "Fail selecting : %s", err_msg);
		log_message(buffer);
		goto exit;
	}

	exit:
	log_message(" ");
	log_message(" ");
	log_message("Import finished!");
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
		log_message("Press NEXT to start...");
		break;
	case EVT_KEYPRESS:
		if (param_one == IV_KEY_PREV) {
			CloseApp();
			return 1;
		}
		else if (param_one == IV_KEY_NEXT) {
			if (step == 0) {
                check_internet();
                theo_server_health_check();
                theo_server_get_max_import_index();
				log_message("Press NEXT to continue...");
			} else if (step == 1) {
                read_quotations();
            }
			else {
				CloseApp();
			}

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