/*
 * Copyright (c) 2018-2023 SignalWire, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libks/ks.h"
#include "libks/ks_json_schema.h"


extern "C" {

KS_DECLARE(const char *) ks_json_schema_status_string(ks_json_schema_status_t status)
{
	switch (status) {
		case KS_JSON_SCHEMA_STATUS_SUCCESS:
			return "Success";
		case KS_JSON_SCHEMA_STATUS_UNAVAILABLE:
			return "JSON Schema validation not enabled";
		case KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA:
			return "Invalid schema";
		case KS_JSON_SCHEMA_STATUS_INVALID_JSON:
			return "Invalid JSON";
		case KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED:
			return "Validation failed";
		case KS_JSON_SCHEMA_STATUS_MEMORY_ERROR:
			return "Memory error";
		case KS_JSON_SCHEMA_STATUS_INVALID_PARAM:
			return "Invalid parameter";
		default:
			return "Unknown error";
	}
}
}

#ifdef HAVE_VALIJSON

#include <valijson/adapters/jsoncpp_adapter.hpp>
#include <valijson/utils/jsoncpp_utils.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>

using std::cerr;
using std::endl;

using valijson::Schema;
using valijson::SchemaParser;
using valijson::adapters::JsonCppAdapter;

struct ks_json_schema {
	std::unique_ptr<valijson::Schema> schema;
};

#if 0
static bool cjson_to_jsoncpp(const ks_json_t *cjson_obj, Json::Value &value)
{
	if (!cjson_obj) {
		return false;
	}

	char *json_string = ks_json_print_unformatted(const_cast<ks_json_t*>(cjson_obj));
	if (!json_string) {
		return false;
	}

	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	std::string parse_errors;

	bool result = reader->parse(json_string, json_string + strlen(json_string), &value, &parse_errors);
	free(json_string);
	return result;
}
#endif

extern "C" {

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create(const char *schema_json, ks_json_schema_t **schema)
{
	if (!schema_json || !schema) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	*schema = nullptr;

	try {
		Json::Value schema_value;
		Json::CharReaderBuilder builder;
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		std::string parse_errors;

		if (!reader->parse(schema_json, schema_json + strlen(schema_json), &schema_value, &parse_errors)) {
			return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
		}
		
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->schema = std::make_unique<valijson::Schema>();

		valijson::SchemaParser parser;
		valijson::adapters::JsonCppAdapter adapter(schema_value);
		
		parser.populateSchema(adapter, *ks_schema->schema);

		*schema = ks_schema.release();
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const std::exception& e) {
		// Check if it's a JSON parse error
		std::string error_msg = e.what();
		if (error_msg.find("parse") != std::string::npos) {
			return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
		}
		return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
	}
}
KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create_from_json(ks_json_t *schema_json, ks_json_schema_t **schema)
{
	(void)schema_json;
	(void)schema;

	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
#if 0
	if (!schema_json || !schema) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	*schema = nullptr;

	try {
		Json::Value schema_value;
		if (!cjson_to_jsoncpp(schema_json, schema_value)) {
			return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
		}
		
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->schema = std::make_unique<valijson::Schema>();

		valijson::SchemaParser parser;
		valijson::adapters::JsonCppAdapter adapter(schema_value);
		
		parser.populateSchema(adapter, *ks_schema->schema);

		*schema = ks_schema.release();
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const std::exception&) {
		return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
	}
#endif 
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_string(ks_json_schema_t *schema, const char *json_string, ks_json_schema_error_t **errors)
{
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
#if 0
	if (!schema || !json_string) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	if (errors) {
		*errors = nullptr;
	}

	try {
		Json::Value json_value;
		Json::CharReaderBuilder builder;
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		std::string parse_errors;

		if (!reader->parse(json_string, json_string + strlen(json_string), &json_value, &parse_errors)) {
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		
		valijson::Validator validator;
		valijson::ValidationResults results;
		valijson::adapters::JsonCppAdapter target(json_value);

		if (validator.validate(*schema->schema, target, &results)) {
			return KS_JSON_SCHEMA_STATUS_SUCCESS;
		}

		if (errors) {
			ks_json_schema_error_t *error_list = nullptr;
			ks_json_schema_error_t *last_error = nullptr;

			auto itr = results.begin();
			while (itr != results.end()) {
				auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
				if (!error) {
					ks_json_schema_error_free(&error_list);
					return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
				}

				std::string path_str;
				for (auto path_itr = itr->context.begin(); path_itr != itr->context.end(); ++path_itr) {
					if (path_itr != itr->context.begin()) {
						path_str += ".";
					}
					path_str += *path_itr;
				}

				error->message = strdup(itr->description.c_str());
				error->path = strdup(path_str.c_str());
				error->next = nullptr;

				if (!error->message || !error->path) {
					free(error->message);
					free(error->path);
					free(error);
					ks_json_schema_error_free(&error_list);
					return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
				}

				if (last_error) {
					last_error->next = error;
				} else {
					error_list = error;
				}
				last_error = error;

				++itr;
			}

			*errors = error_list;
		}

		return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
	} catch (const std::exception& e) {
		// Check if it's a JSON parse error
		std::string error_msg = e.what();
		if (error_msg.find("parse") != std::string::npos) {
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
	}
#endif
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_json(ks_json_schema_t *schema, ks_json_t *json, ks_json_schema_error_t **errors)
{
	(void)schema;
	(void)json;
	(void)errors;

	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
#if 0
	if (!schema || !json) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	if (errors) {
		*errors = nullptr;
	}

	try {
		Json::Value json_value;
		if (!cjson_to_jsoncpp(json, json_value)) {
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		
		valijson::Validator validator;
		valijson::ValidationResults results;
		valijson::adapters::JsonCppAdapter target(json_value);

		if (validator.validate(*schema->schema, target, &results)) {
			return KS_JSON_SCHEMA_STATUS_SUCCESS;
		}

		if (errors) {
			ks_json_schema_error_t *error_list = nullptr;
			ks_json_schema_error_t *last_error = nullptr;

			auto itr = results.begin();
			while (itr != results.end()) {
				auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
				if (!error) {
					ks_json_schema_error_free(&error_list);
					return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
				}

				std::string path_str;
				for (auto path_itr = itr->context.begin(); path_itr != itr->context.end(); ++path_itr) {
					if (path_itr != itr->context.begin()) {
						path_str += ".";
					}
					path_str += *path_itr;
				}

				error->message = strdup(itr->description.c_str());
				error->path = strdup(path_str.c_str());
				error->next = nullptr;

				if (!error->message || !error->path) {
					free(error->message);
					free(error->path);
					free(error);
					ks_json_schema_error_free(&error_list);
					return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
				}

				if (last_error) {
					last_error->next = error;
				} else {
					error_list = error;
				}
				last_error = error;

				++itr;
			}

			*errors = error_list;
		}

		return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
	} catch (const std::exception&) {
		return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
	}
#endif
}

KS_DECLARE(void) ks_json_schema_destroy(ks_json_schema_t **schema)
{
	if (!schema || !*schema) {
		return;
	}

	delete *schema;
	*schema = nullptr;
}

KS_DECLARE(void) ks_json_schema_error_free(ks_json_schema_error_t **errors)
{
	if (!errors || !*errors) {
		return;
	}

	ks_json_schema_error_t *current = *errors;
	while (current) {
		ks_json_schema_error_t *next = current->next;
		free(current->message);
		free(current->path);
		free(current);
		current = next;
	}

	*errors = nullptr;
}

}

#else /* !HAVE_VALIJSON */

extern "C" {

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create(const char *schema_json, ks_json_schema_t **schema)
{
	(void)schema_json;
	(void)schema;
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create_from_json(ks_json_t *schema_json, ks_json_schema_t **schema)
{
	(void)schema_json;
	(void)schema;
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_string(ks_json_schema_t *schema, const char *json_string, ks_json_schema_error_t **errors)
{
	(void)schema;
	(void)json_string;
	(void)errors;
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_json(ks_json_schema_t *schema, ks_json_t *json, ks_json_schema_error_t **errors)
{
	(void)schema;
	(void)json;
	(void)errors;
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
}

KS_DECLARE(void) ks_json_schema_destroy(ks_json_schema_t **schema)
{
	(void)schema;
}

KS_DECLARE(void) ks_json_schema_error_free(ks_json_schema_error_t **errors)
{
	(void)errors;
}

}

#endif /* HAVE_VALIJSON */

/* For Emacs:
 * Local Variables:
 * mode:c++
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */