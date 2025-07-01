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


#ifdef HAVE_JSON_SCHEMA_VALIDATOR

#include <memory>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "libks/ks.h"

struct ks_json_schema {
	std::unique_ptr<nlohmann::json_schema::json_validator> validator;
};

static bool cjson_to_nlohmann(const ks_json_t *cjson_obj, nlohmann::json &json_obj)
{
	if (!cjson_obj) {
		return false;
	}

	char *json_string = ks_json_print_unformatted(const_cast<ks_json_t*>(cjson_obj));
	if (!json_string) {
		return false;
	}

	try {
		json_obj = nlohmann::json::parse(json_string);
		free(json_string);
		return true;
	} catch (const std::exception&) {
		free(json_string);
		return false;
	}
}

static void schema_format_checker(const std::string &format, const std::string &value)
{
	nlohmann::json_schema::default_string_format_check(format, value);
	/* could extend to check more types */
}

class ks_error_handler : public nlohmann::json_schema::basic_error_handler
{
public:
	struct validation_error {
		std::string message;
		std::string path;
	};
	
	std::vector<validation_error> errors;
	static const size_t MAX_ERRORS = 5;
	
	void error(const nlohmann::json::json_pointer &pointer, const nlohmann::json &instance, const std::string &message) override
	{
		// Stop collecting errors after reaching the limit
		if (errors.size() >= MAX_ERRORS) {
			return;
		}
		
		validation_error err;
		err.message = message;
		err.path = pointer.to_string();
		errors.push_back(err);
		
		// Add a truncation message if we hit the limit
		if (errors.size() == MAX_ERRORS) {
			validation_error truncated_err;
			truncated_err.message = "Maximum error limit reached. Additional errors may exist.";
			truncated_err.path = "";
			errors.push_back(truncated_err);
		}
		
		basic_error_handler::error(pointer, instance, message);
	}
};


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

#ifdef HAVE_JSON_SCHEMA_VALIDATOR

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
		// Parse the schema JSON
		nlohmann::json schema_json_obj = nlohmann::json::parse(schema_json);
		
		// Create validator with remote reference support
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->validator = std::make_unique<nlohmann::json_schema::json_validator>(nullptr, schema_format_checker);
		
		// Set schema with remote reference loading enabled - this will validate the schema
		ks_schema->validator->set_root_schema(schema_json_obj);

		*schema = ks_schema.release();
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const nlohmann::json::parse_error& e) {
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				std::string msg = "JSON parse error in schema: " + std::string(e.what());
				error->message = strdup(msg.c_str());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA;
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
		nlohmann::json schema_json_obj;
		if (!cjson_to_nlohmann(schema_json, schema_json_obj)) {
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
		
		// Create validator with remote reference support
		auto ks_schema = std::make_unique<ks_json_schema>();
		ks_schema->validator = std::make_unique<nlohmann::json_schema::json_validator>(nullptr, schema_format_checker);
		
		// Set schema with remote reference loading enabled - this will validate the schema
		ks_schema->validator->set_root_schema(schema_json_obj);

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
		// Parse the JSON string
		nlohmann::json json_doc = nlohmann::json::parse(json_string);
		
		// Create error handler to collect detailed validation errors
		ks_error_handler error_handler;
		
		// Validate using json-schema-validator with error handler
		schema->validator->validate(json_doc, error_handler);
		
		// Check if validation failed
		if (!error_handler.errors.empty()) {
			if (errors) {
				ks_json_schema_error_t *error_list = nullptr;
				ks_json_schema_error_t *last_error = nullptr;

				for (const auto& validation_error : error_handler.errors) {
					auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
					if (!error) {
						ks_json_schema_error_free(&error_list);
						return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
					}

					error->message = strdup(validation_error.message.c_str());
					error->path = strdup(validation_error.path.c_str());
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
				}

				*errors = error_list;
			}
			return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
		}
		
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const nlohmann::json::parse_error& e) {
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				std::string msg = "JSON parse error: " + std::string(e.what());
				error->message = strdup(msg.c_str());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
	} catch (const std::exception& e) {
		// Other validation errors
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				error->message = strdup(e.what());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
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
		nlohmann::json json_doc;
		if (!cjson_to_nlohmann(json, json_doc)) {
			if (errors) {
				auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
				if (error) {
					error->message = strdup("Failed to convert JSON object");
					error->path = strdup("");
					error->next = nullptr;
					*errors = error;
				}
			}
			return KS_JSON_SCHEMA_STATUS_INVALID_JSON;
		}
		
		// Create error handler to collect detailed validation errors
		ks_error_handler error_handler;
		
		// Validate using json-schema-validator with error handler
		schema->validator->validate(json_doc, error_handler);
		
		// Check if validation failed
		if (!error_handler.errors.empty()) {
			if (errors) {
				ks_json_schema_error_t *error_list = nullptr;
				ks_json_schema_error_t *last_error = nullptr;

				for (const auto& validation_error : error_handler.errors) {
					auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
					if (!error) {
						ks_json_schema_error_free(&error_list);
						return KS_JSON_SCHEMA_STATUS_MEMORY_ERROR;
					}

					error->message = strdup(validation_error.message.c_str());
					error->path = strdup(validation_error.path.c_str());
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
				}

				*errors = error_list;
			}
			return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
		}
		
		return KS_JSON_SCHEMA_STATUS_SUCCESS;
	} catch (const std::exception& e) {
		// Other validation errors
		if (errors) {
			auto error = static_cast<ks_json_schema_error_t *>(malloc(sizeof(ks_json_schema_error_t)));
			if (error) {
				error->message = strdup(e.what());
				error->path = strdup("");
				error->next = nullptr;
				*errors = error;
			}
		}
		return KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED;
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