/*
 * Copyright (c) 2025 SignalWire, Inc
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


#ifdef HAVE_VALIJSON

#include <memory>
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <valijson/adapters/rapidjson_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>
#include <valijson/validation_results.hpp>

#include "libks/ks.h"

struct ks_json_schema {
	std::unique_ptr<valijson::Schema> schema;
};

static bool cjson_to_rapidjson(const ks_json_t *cjson_obj, rapidjson::Document &doc)
{
	if (!cjson_obj) {
		return false;
	}

	char *json_string = ks_json_print_unformatted(const_cast<ks_json_t*>(cjson_obj));
	if (!json_string) {
		return false;
	}

	bool result = !doc.Parse(json_string).HasParseError();
	free(json_string);
	return result;
}

#else

#include "libks/ks.h"

#endif

KS_BEGIN_EXTERN_C

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

#ifdef HAVE_VALIJSON

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create(const char *schema_json, ks_json_schema_t **schema, ks_json_schema_error_t **errors)
{
	if (!schema_json || !schema) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	*schema = nullptr;
	if (errors) {
		*errors = nullptr;
	}

	try {
		rapidjson::Document schema_doc;
		if (schema_doc.Parse(schema_json).HasParseError()) {
			if (errors) {
				auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
				if (error) {
					error->message = strdup("JSON parse error in schema");
					error->path = strdup("");
					error->next = nullptr;
					*errors = error;
				}
			}
			return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
		}
		
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->schema = std::make_unique<valijson::Schema>();

		valijson::SchemaParser parser;
		valijson::adapters::RapidJsonAdapter adapter(schema_doc);
		
		parser.populateSchema(adapter, *ks_schema->schema);

		*schema = ks_schema.release();
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const std::exception& e) {
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				error->message = strdup(e.what());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
	}
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create_from_json(ks_json_t *schema_json, ks_json_schema_t **schema, ks_json_schema_error_t **errors)
{
	if (!schema_json || !schema) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	*schema = nullptr;
	if (errors) {
		*errors = nullptr;
	}

	try {
		rapidjson::Document schema_doc;
		if (!cjson_to_rapidjson(schema_json, schema_doc)) {
			if (errors) {
				auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
				if (error) {
					error->message = strdup("Failed to convert JSON object to schema format");
					error->path = strdup("");
					error->next = nullptr;
					*errors = error;
				}
			}
			return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
		}
		
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->schema = std::make_unique<valijson::Schema>();

		valijson::SchemaParser parser;
		valijson::adapters::RapidJsonAdapter adapter(schema_doc);
		
		parser.populateSchema(adapter, *ks_schema->schema);

		*schema = ks_schema.release();
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const std::exception& e) {
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				error->message = strdup(e.what());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
	}
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_string(ks_json_schema_t *schema, const char *json_string, ks_json_schema_error_t **errors)
{
	if (!schema || !json_string) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	if (errors) {
		*errors = nullptr;
	}

	try {
		rapidjson::Document json_doc;
		if (json_doc.Parse(json_string).HasParseError()) {
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		
		valijson::Validator validator;
		valijson::ValidationResults results;
		valijson::adapters::RapidJsonAdapter target(json_doc);

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
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_validate_json(ks_json_schema_t *schema, ks_json_t *json, ks_json_schema_error_t **errors)
{
	if (!schema || !json) {
		return KS_JSON_SCHEMA_STATUS_INVALID_PARAM;
	}

	if (errors) {
		*errors = nullptr;
	}

	try {
		rapidjson::Document json_doc;
		if (!cjson_to_rapidjson(json, json_doc)) {
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		
		valijson::Validator validator;
		valijson::ValidationResults results;
		valijson::adapters::RapidJsonAdapter target(json_doc);

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
}

KS_DECLARE(void) ks_json_schema_destroy(ks_json_schema_t **schema)
{
	if (!schema || !*schema) {
		return;
	}

	delete *schema;
	*schema = nullptr;
}

#else

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create(const char *schema_json, ks_json_schema_t **schema, ks_json_schema_error_t **errors)
{
	(void)schema_json;
	(void)schema;
	(void)errors;
	return KS_JSON_SCHEMA_STATUS_UNAVAILABLE;
}

KS_DECLARE(ks_json_schema_status_t) ks_json_schema_create_from_json(ks_json_t *schema_json, ks_json_schema_t **schema, ks_json_schema_error_t **errors)
{
	(void)schema_json;
	(void)schema;
	(void)errors;
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

#endif

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

KS_END_EXTERN_C

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