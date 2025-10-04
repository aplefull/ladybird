/*
 * Copyright (c) 2020, Matthew Olsson <mattco@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/AST.h>
#include <LibJS/Export.h>
#include <LibJS/Runtime/Object.h>

namespace JS {

class JS_API JSONObject final : public Object {
    JS_OBJECT(JSONObject, Object);
    GC_DECLARE_ALLOCATOR(JSONObject);

public:
    virtual void initialize(Realm&) override;
    virtual ~JSONObject() override = default;

    // The base implementation of stringify is exposed because it is used by
    // test-js to communicate between the JS tests and the C++ test runner.
    static ThrowCompletionOr<Optional<String>> stringify_impl(VM&, Value value, Value replacer, Value space);

    static ThrowCompletionOr<Value> parse_json(VM&, StringView text);
    static Value parse_json_value(VM&, JsonValue const&);

private:
    explicit JSONObject(Realm&);

    struct StringifyState {
        GC::Ptr<FunctionObject> replacer_function;
        HashTable<GC::Ptr<Object>> seen_objects;
        String indent;
        String gap;
        Optional<Vector<Utf16String>> property_list;
    };

    // 1.2.1 JSON Parse Record, https://tc39.es/proposal-json-parse-with-source/#sec-json-parse-record
    struct JSONParseRecord {
        // [[ParseNode]]: a Parse Node (we store the Expression AST node)
        RefPtr<Expression const> parse_node;

        // [[Key]]: a property key
        PropertyKey key;

        // [[Value]]: an ECMAScript language value
        Value value;

        // [[Elements]]: a List of JSON Parse Records
        Vector<JSONParseRecord> elements;

        // [[Entries]]: a List of JSON Parse Records
        Vector<JSONParseRecord> entries;

        JSONParseRecord(RefPtr<Expression const> node, PropertyKey k, Value v, Vector<JSONParseRecord> elems = {}, Vector<JSONParseRecord> ents = {})
            : parse_node(move(node))
            , key(k)
            , value(v)
            , elements(move(elems))
            , entries(move(ents))
        {
        }
    };

    // Stringify helpers
    static ThrowCompletionOr<Optional<String>> serialize_json_property(VM&, StringifyState&, PropertyKey const& key, Object* holder);
    static ThrowCompletionOr<String> serialize_json_object(VM&, StringifyState&, Object&);
    static ThrowCompletionOr<String> serialize_json_array(VM&, StringifyState&, Object&);
    static String quote_json_string(Utf16View const&);

    // Parse helpers
    static Object* parse_json_object(VM&, JsonObject const&);
    static Array* parse_json_array(VM&, JsonArray const&);
    static JSONParseRecord create_json_parse_record(VM&, RefPtr<Expression const> parse_node, PropertyKey const& key, Value const& val, Utf16View const& original_source);
    static ThrowCompletionOr<Value> internalize_json_property(VM&, Object* holder, PropertyKey const& name, FunctionObject& reviver, Optional<JSONParseRecord> parse_record, Utf16View const& original_source);

    JS_DECLARE_NATIVE_FUNCTION(stringify);
    JS_DECLARE_NATIVE_FUNCTION(parse);
    JS_DECLARE_NATIVE_FUNCTION(raw_json);
    JS_DECLARE_NATIVE_FUNCTION(is_raw_json);
};

}
