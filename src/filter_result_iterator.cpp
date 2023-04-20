#include <queue>
#include <id_list.h>
#include "filter_result_iterator.h"
#include "index.h"
#include "posting.h"
#include "collection_manager.h"

void filter_result_t::and_filter_results(const filter_result_t& a, const filter_result_t& b, filter_result_t& result) {
    auto lenA = a.count, lenB = b.count;
    if (lenA == 0 || lenB == 0) {
        return;
    }

    result.docs = new uint32_t[std::min(lenA, lenB)];

    auto A = a.docs, B = b.docs, out = result.docs;
    const uint32_t *endA = A + lenA;
    const uint32_t *endB = B + lenB;

    // Add an entry of references in the result for each unique collection in a and b.
    for (auto const& item: a.reference_filter_results) {
        if (result.reference_filter_results.count(item.first) == 0) {
            result.reference_filter_results[item.first] = new reference_filter_result_t[std::min(lenA, lenB)];
        }
    }
    for (auto const& item: b.reference_filter_results) {
        if (result.reference_filter_results.count(item.first) == 0) {
            result.reference_filter_results[item.first] = new reference_filter_result_t[std::min(lenA, lenB)];
        }
    }

    while (true) {
        while (*A < *B) {
            SKIP_FIRST_COMPARE:
            if (++A == endA) {
                result.count = out - result.docs;
                return;
            }
        }
        while (*A > *B) {
            if (++B == endB) {
                result.count = out - result.docs;
                return;
            }
        }
        if (*A == *B) {
            *out = *A;

            // Copy the references of the document from every collection into result.
            for (auto const& item: a.reference_filter_results) {
                result.reference_filter_results[item.first][out - result.docs] = item.second[A - a.docs];
            }
            for (auto const& item: b.reference_filter_results) {
                result.reference_filter_results[item.first][out - result.docs] = item.second[B - b.docs];
            }

            out++;

            if (++A == endA || ++B == endB) {
                result.count = out - result.docs;
                return;
            }
        } else {
            goto SKIP_FIRST_COMPARE;
        }
    }
}

void filter_result_t::or_filter_results(const filter_result_t& a, const filter_result_t& b, filter_result_t& result) {
    if (a.count == 0 && b.count == 0) {
        return;
    }

    // If either one of a or b does not have any matches, copy other into result.
    if (a.count == 0) {
        result = b;
        return;
    }
    if (b.count == 0) {
        result = a;
        return;
    }

    size_t indexA = 0, indexB = 0, res_index = 0, lenA = a.count, lenB = b.count;
    result.docs = new uint32_t[lenA + lenB];

    // Add an entry of references in the result for each unique collection in a and b.
    for (auto const& item: a.reference_filter_results) {
        if (result.reference_filter_results.count(item.first) == 0) {
            result.reference_filter_results[item.first] = new reference_filter_result_t[lenA + lenB];
        }
    }
    for (auto const& item: b.reference_filter_results) {
        if (result.reference_filter_results.count(item.first) == 0) {
            result.reference_filter_results[item.first] = new reference_filter_result_t[lenA + lenB];
        }
    }

    while (indexA < lenA && indexB < lenB) {
        if (a.docs[indexA] < b.docs[indexB]) {
            // check for duplicate
            if (res_index == 0 || result.docs[res_index - 1] != a.docs[indexA]) {
                result.docs[res_index] = a.docs[indexA];
                res_index++;
            }

            // Copy references of the last result document from every collection in a.
            for (auto const& item: a.reference_filter_results) {
                result.reference_filter_results[item.first][res_index - 1] = item.second[indexA];
            }

            indexA++;
        } else {
            if (res_index == 0 || result.docs[res_index - 1] != b.docs[indexB]) {
                result.docs[res_index] = b.docs[indexB];
                res_index++;
            }

            for (auto const& item: b.reference_filter_results) {
                result.reference_filter_results[item.first][res_index - 1] = item.second[indexB];
            }

            indexB++;
        }
    }

    while (indexA < lenA) {
        if (res_index == 0 || result.docs[res_index - 1] != a.docs[indexA]) {
            result.docs[res_index] = a.docs[indexA];
            res_index++;
        }

        for (auto const& item: a.reference_filter_results) {
            result.reference_filter_results[item.first][res_index - 1] = item.second[indexA];
        }

        indexA++;
    }

    while (indexB < lenB) {
        if(res_index == 0 || result.docs[res_index - 1] != b.docs[indexB]) {
            result.docs[res_index] = b.docs[indexB];
            res_index++;
        }

        for (auto const& item: b.reference_filter_results) {
            result.reference_filter_results[item.first][res_index - 1] = item.second[indexB];
        }

        indexB++;
    }

    result.count = res_index;

    // shrink fit
    auto out = new uint32_t[res_index];
    memcpy(out, result.docs, res_index * sizeof(uint32_t));
    delete[] result.docs;
    result.docs = out;

    for (auto &item: result.reference_filter_results) {
        auto out_references = new reference_filter_result_t[res_index];

        for (uint32_t i = 0; i < result.count; i++) {
            out_references[i] = item.second[i];
        }
        delete[] item.second;
        item.second = out_references;
    }
}

void filter_result_iterator_t::and_filter_iterators() {
    while (left_it->is_valid && right_it->is_valid) {
        while (left_it->seq_id < right_it->seq_id) {
            left_it->next();
            if (!left_it->is_valid) {
                is_valid = false;
                return;
            }
        }

        while (left_it->seq_id > right_it->seq_id) {
            right_it->next();
            if (!right_it->is_valid) {
                is_valid = false;
                return;
            }
        }

        if (left_it->seq_id == right_it->seq_id) {
            seq_id = left_it->seq_id;
            reference.clear();

            for (const auto& item: left_it->reference) {
                reference[item.first] = item.second;
            }
            for (const auto& item: right_it->reference) {
                reference[item.first] = item.second;
            }

            return;
        }
    }

    is_valid = false;
}

void filter_result_iterator_t::or_filter_iterators() {
    if (left_it->is_valid && right_it->is_valid) {
        if (left_it->seq_id < right_it->seq_id) {
            seq_id = left_it->seq_id;
            reference.clear();

            for (const auto& item: left_it->reference) {
                reference[item.first] = item.second;
            }

            return;
        }

        if (left_it->seq_id > right_it->seq_id) {
            seq_id = right_it->seq_id;
            reference.clear();

            for (const auto& item: right_it->reference) {
                reference[item.first] = item.second;
            }

            return;
        }

        seq_id = left_it->seq_id;
        reference.clear();

        for (const auto& item: left_it->reference) {
            reference[item.first] = item.second;
        }
        for (const auto& item: right_it->reference) {
            reference[item.first] = item.second;
        }

        return;
    }

    if (left_it->is_valid) {
        seq_id = left_it->seq_id;
        reference.clear();

        for (const auto& item: left_it->reference) {
            reference[item.first] = item.second;
        }

        return;
    }

    if (right_it->is_valid) {
        seq_id = right_it->seq_id;
        reference.clear();

        for (const auto& item: right_it->reference) {
            reference[item.first] = item.second;
        }

        return;
    }

    is_valid = false;
}

void filter_result_iterator_t::advance_string_filter_token_iterators() {
    for (uint32_t i = 0; i < posting_list_iterators.size(); i++) {
        auto& filter_value_tokens = posting_list_iterators[i];

        if (!filter_value_tokens[0].valid() || filter_value_tokens[0].id() != seq_id) {
            continue;
        }

        for (auto& iter: filter_value_tokens) {
            if (iter.valid()) {
                iter.next();
            }
        }
    }
}

void filter_result_iterator_t::doc_matching_string_filter(bool field_is_array) {
    // If none of the filter value iterators are valid, mark this node as invalid.
    bool one_is_valid = false;

    // Since we do OR between filter values, the lowest seq_id id from all is selected.
    uint32_t lowest_id = UINT32_MAX;

    if (filter_node->filter_exp.comparators[0] == EQUALS || filter_node->filter_exp.comparators[0] == NOT_EQUALS) {
        for (auto& filter_value_tokens : posting_list_iterators) {
            bool tokens_iter_is_valid, exact_match = false;
            while(true) {
                // Perform AND between tokens of a filter value.
                posting_list_t::intersect(filter_value_tokens, tokens_iter_is_valid);

                if (!tokens_iter_is_valid) {
                    break;
                }

                if (posting_list_t::has_exact_match(filter_value_tokens, field_is_array)) {
                    exact_match = true;
                    break;
                } else {
                    // Keep advancing token iterators till exact match is not found.
                    for (auto &item: filter_value_tokens) {
                        item.next();
                    }
                }
            }

            one_is_valid = tokens_iter_is_valid || one_is_valid;

            if (tokens_iter_is_valid && exact_match && filter_value_tokens[0].id() < lowest_id) {
                lowest_id = filter_value_tokens[0].id();
            }
        }
    } else {
        for (auto& filter_value_tokens : posting_list_iterators) {
            // Perform AND between tokens of a filter value.
            bool tokens_iter_is_valid;
            posting_list_t::intersect(filter_value_tokens, tokens_iter_is_valid);

            one_is_valid = tokens_iter_is_valid || one_is_valid;

            if (tokens_iter_is_valid && filter_value_tokens[0].id() < lowest_id) {
                lowest_id = filter_value_tokens[0].id();
            }
        }
    }

    if (one_is_valid) {
        seq_id = lowest_id;
    }

    is_valid = one_is_valid;
}

void filter_result_iterator_t::next() {
    if (!is_valid) {
        return;
    }

    if (filter_node->isOperator) {
        // Advance the subtrees and then apply operators to arrive at the next valid doc.
        if (filter_node->filter_operator == AND) {
            left_it->next();
            right_it->next();
            and_filter_iterators();
        } else {
            if (left_it->seq_id == seq_id && right_it->seq_id == seq_id) {
                left_it->next();
                right_it->next();
            } else if (left_it->seq_id == seq_id) {
                left_it->next();
            } else {
                right_it->next();
            }

            or_filter_iterators();
        }

        return;
    }

    if (is_filter_result_initialized) {
        if (++result_index >= filter_result.count) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        reference.clear();
        for (auto const& item: filter_result.reference_filter_results) {
            reference[item.first] = item.second[result_index];
        }

        return;
    }

    const filter a_filter = filter_node->filter_exp;

    if (!index->field_is_indexed(a_filter.field_name)) {
        is_valid = false;
        return;
    }

    field f = index->search_schema.at(a_filter.field_name);

    if (f.is_string()) {
        if (filter_node->filter_exp.apply_not_equals) {
            if (++seq_id < result_index) {
                return;
            }

            uint32_t previous_match;
            do {
                previous_match = seq_id;
                advance_string_filter_token_iterators();
                doc_matching_string_filter(f.is_array());
            } while (is_valid && previous_match + 1 == seq_id);

            if (!is_valid) {
                // We've reached the end of the index, no possible matches pending.
                if (previous_match >= index->seq_ids->last_id()) {
                    return;
                }

                is_valid = true;
                result_index = index->seq_ids->last_id() + 1;
                seq_id = previous_match + 1;
                return;
            }

            result_index = seq_id;
            seq_id = previous_match + 1;
            return;
        }

        advance_string_filter_token_iterators();
        doc_matching_string_filter(f.is_array());

        return;
    }
}

void numeric_not_equals_filter(num_tree_t* const num_tree,
                               const int64_t value,
                               uint32_t*&& all_ids,
                               uint32_t&& all_ids_length,
                               uint32_t*& result_ids,
                               size_t& result_ids_len) {
    uint32_t* to_exclude_ids = nullptr;
    size_t to_exclude_ids_len = 0;

    num_tree->search(EQUALS, value, &to_exclude_ids, to_exclude_ids_len);

    result_ids_len = ArrayUtils::exclude_scalar(all_ids, all_ids_length, to_exclude_ids, to_exclude_ids_len, &result_ids);

    delete[] all_ids;
    delete[] to_exclude_ids;
}

void apply_not_equals(uint32_t*&& all_ids,
                      uint32_t&& all_ids_length,
                      uint32_t*& result_ids,
                      uint32_t& result_ids_len) {

    uint32_t* to_include_ids = nullptr;
    size_t to_include_ids_len = 0;

    to_include_ids_len = ArrayUtils::exclude_scalar(all_ids, all_ids_length, result_ids,
                                                    result_ids_len, &to_include_ids);

    delete[] all_ids;
    delete[] result_ids;

    result_ids = to_include_ids;
    result_ids_len = to_include_ids_len;
}

void filter_result_iterator_t::init() {
    if (filter_node == nullptr) {
        return;
    }

    if (filter_node->isOperator) {
        if (filter_node->filter_operator == AND) {
            and_filter_iterators();
        } else {
            or_filter_iterators();
        }

        return;
    }

    const filter a_filter = filter_node->filter_exp;

    bool is_referenced_filter = !a_filter.referenced_collection_name.empty();
    if (is_referenced_filter) {
        // Apply filter on referenced collection and get the sequence ids of current collection from the filtered documents.
        auto& cm = CollectionManager::get_instance();
        auto collection = cm.get_collection(a_filter.referenced_collection_name);
        if (collection == nullptr) {
            status = Option<bool>(400, "Referenced collection `" + a_filter.referenced_collection_name + "` not found.");
            is_valid = false;
            return;
        }

        auto reference_filter_op = collection->get_reference_filter_ids(a_filter.field_name,
                                                                        filter_result,
                                                                        collection_name);
        if (!reference_filter_op.ok()) {
            status = Option<bool>(400, "Failed to apply reference filter on `" + a_filter.referenced_collection_name
                                       + "` collection: " + reference_filter_op.error());
            is_valid = false;
            return;
        }

        if (filter_result.count == 0) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        for (auto const& item: filter_result.reference_filter_results) {
            reference[item.first] = item.second[result_index];
        }

        is_filter_result_initialized = true;
        return;
    }

    if (a_filter.field_name == "id") {
        if (a_filter.values.empty()) {
            is_valid = false;
            return;
        }

        // we handle `ids` separately
        std::vector<uint32_t> result_ids;
        for (const auto& id_str : a_filter.values) {
            result_ids.push_back(std::stoul(id_str));
        }

        std::sort(result_ids.begin(), result_ids.end());

        filter_result.count = result_ids.size();
        filter_result.docs = new uint32_t[result_ids.size()];
        std::copy(result_ids.begin(), result_ids.end(), filter_result.docs);

        seq_id = filter_result.docs[result_index];
        is_filter_result_initialized = true;
        return;
    } else if (a_filter.field_name == Index::SEQ_IDS_FIELD) {
        if (index->seq_ids->num_ids() == 0) {
            is_valid = false;
            return;
        }

        filter_result.count = index->seq_ids->num_ids();
        filter_result.docs = index->seq_ids->uncompress();

        seq_id = filter_result.docs[result_index];
        is_filter_result_initialized = true;
        return;
    }

    if (!index->field_is_indexed(a_filter.field_name)) {
        is_valid = false;
        return;
    }

    field f = index->search_schema.at(a_filter.field_name);

    if (f.is_integer()) {
        auto num_tree = index->numerical_index.at(a_filter.field_name);

        for (size_t fi = 0; fi < a_filter.values.size(); fi++) {
            const std::string& filter_value = a_filter.values[fi];
            int64_t value = (int64_t)std::stol(filter_value);

            size_t result_size = filter_result.count;
            if (a_filter.comparators[fi] == RANGE_INCLUSIVE && fi+1 < a_filter.values.size()) {
                const std::string& next_filter_value = a_filter.values[fi + 1];
                auto const range_end_value = (int64_t)std::stol(next_filter_value);
                num_tree->range_inclusive_search(value, range_end_value, &filter_result.docs, result_size);
                fi++;
            } else if (a_filter.comparators[fi] == NOT_EQUALS) {
                numeric_not_equals_filter(num_tree, value,
                                          index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                                          filter_result.docs, result_size);
            } else {
                num_tree->search(a_filter.comparators[fi], value, &filter_result.docs, result_size);
            }

            filter_result.count = result_size;
        }

        if (a_filter.apply_not_equals) {
            apply_not_equals(index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                             filter_result.docs, filter_result.count);
        }

        if (filter_result.count == 0) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        is_filter_result_initialized = true;
        return;
    } else if (f.is_float()) {
        auto num_tree = index->numerical_index.at(a_filter.field_name);

        for (size_t fi = 0; fi < a_filter.values.size(); fi++) {
            const std::string& filter_value = a_filter.values[fi];
            float value = (float)std::atof(filter_value.c_str());
            int64_t float_int64 = Index::float_to_int64_t(value);

            size_t result_size = filter_result.count;
            if (a_filter.comparators[fi] == RANGE_INCLUSIVE && fi+1 < a_filter.values.size()) {
                const std::string& next_filter_value = a_filter.values[fi+1];
                int64_t range_end_value = Index::float_to_int64_t((float) std::atof(next_filter_value.c_str()));
                num_tree->range_inclusive_search(float_int64, range_end_value, &filter_result.docs, result_size);
                fi++;
            } else if (a_filter.comparators[fi] == NOT_EQUALS) {
                numeric_not_equals_filter(num_tree, float_int64,
                                          index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                                          filter_result.docs, result_size);
            } else {
                num_tree->search(a_filter.comparators[fi], float_int64, &filter_result.docs, result_size);
            }

            filter_result.count = result_size;
        }

        if (a_filter.apply_not_equals) {
            apply_not_equals(index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                             filter_result.docs, filter_result.count);
        }

        if (filter_result.count == 0) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        is_filter_result_initialized = true;
        return;
    } else if (f.is_bool()) {
        auto num_tree = index->numerical_index.at(a_filter.field_name);

        size_t value_index = 0;
        for (const std::string& filter_value : a_filter.values) {
            int64_t bool_int64 = (filter_value == "1") ? 1 : 0;

            size_t result_size = filter_result.count;
            if (a_filter.comparators[value_index] == NOT_EQUALS) {
                numeric_not_equals_filter(num_tree, bool_int64,
                                          index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                                          filter_result.docs, result_size);
            } else {
                num_tree->search(a_filter.comparators[value_index], bool_int64, &filter_result.docs, result_size);
            }

            filter_result.count = result_size;
            value_index++;
        }

        if (a_filter.apply_not_equals) {
            apply_not_equals(index->seq_ids->uncompress(), index->seq_ids->num_ids(),
                             filter_result.docs, filter_result.count);
        }

        if (filter_result.count == 0) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        is_filter_result_initialized = true;
        return;
    } else if (f.is_string()) {
        art_tree* t = index->search_index.at(a_filter.field_name);

        for (const std::string& filter_value : a_filter.values) {
            std::vector<void*> posting_lists;

            // there could be multiple tokens in a filter value, which we have to treat as ANDs
            // e.g. country: South Africa
            Tokenizer tokenizer(filter_value, true, false, f.locale, index->symbols_to_index, index->token_separators);

            std::string str_token;
            size_t token_index = 0;
            std::vector<std::string> str_tokens;

            while (tokenizer.next(str_token, token_index)) {
                str_tokens.push_back(str_token);

                art_leaf* leaf = (art_leaf *) art_search(t, (const unsigned char*) str_token.c_str(),
                                                         str_token.length()+1);
                if (leaf == nullptr) {
                    continue;
                }

                posting_lists.push_back(leaf->values);
            }

            if (posting_lists.size() != str_tokens.size()) {
                continue;
            }

            std::vector<posting_list_t*> plists;
            posting_t::to_expanded_plists(posting_lists, plists, expanded_plists);

            posting_list_iterators.emplace_back(std::vector<posting_list_t::iterator_t>());

            for (auto const& plist: plists) {
                posting_list_iterators.back().push_back(plist->new_iterator());
            }
        }

        doc_matching_string_filter(f.is_array());

        if (filter_node->filter_exp.apply_not_equals) {
            // filter didn't match any id. So by applying not equals, every id in the index is a match.
            if (!is_valid) {
                is_valid = true;
                seq_id = 0;
                result_index = index->seq_ids->last_id() + 1;
                return;
            }

            // [0, seq_id) are a match for not equals.
            if (seq_id > 0) {
                result_index = seq_id;
                seq_id = 0;
                return;
            }

            // Keep ignoring the consecutive matches.
            uint32_t previous_match;
            do {
                previous_match = seq_id;
                advance_string_filter_token_iterators();
                doc_matching_string_filter(f.is_array());
            } while (is_valid && previous_match + 1 == seq_id);

            if (!is_valid) {
                // filter matched all the ids in the index. So for not equals, there's no match.
                if (previous_match >= index->seq_ids->last_id()) {
                    return;
                }

                is_valid = true;
                result_index = index->seq_ids->last_id() + 1;
                seq_id = previous_match + 1;
                return;
            }

            result_index = seq_id;
            seq_id = previous_match + 1;
        }

        return;
    }
}

bool filter_result_iterator_t::valid() {
    if (!is_valid) {
        return false;
    }

    if (filter_node->isOperator) {
        if (filter_node->filter_operator == AND) {
            is_valid = left_it->valid() && right_it->valid();
            return is_valid;
        } else {
            is_valid = left_it->valid() || right_it->valid();
            return is_valid;
        }
    }

    if (is_filter_result_initialized) {
        is_valid = result_index < filter_result.count;
        return is_valid;
    }

    const filter a_filter = filter_node->filter_exp;

    if (!index->field_is_indexed(a_filter.field_name)) {
        is_valid = false;
        return is_valid;
    }

    field f = index->search_schema.at(a_filter.field_name);

    if (f.is_string()) {
        if (filter_node->filter_exp.apply_not_equals) {
            return seq_id < result_index;
        }

        bool one_is_valid = false;
        for (auto& filter_value_tokens: posting_list_iterators) {
            posting_list_t::intersect(filter_value_tokens, one_is_valid);

            if (one_is_valid) {
                break;
            }
        }

        is_valid = one_is_valid;
        return is_valid;
    }

    return false;
}

void filter_result_iterator_t::skip_to(uint32_t id) {
    if (!is_valid) {
        return;
    }

    if (filter_node->isOperator) {
        // Skip the subtrees to id and then apply operators to arrive at the next valid doc.
        left_it->skip_to(id);
        right_it->skip_to(id);

        if (filter_node->filter_operator == AND) {
            and_filter_iterators();
        } else {
            or_filter_iterators();
        }

        return;
    }

    if (is_filter_result_initialized) {
        while (filter_result.docs[result_index] < id && ++result_index < filter_result.count);

        if (result_index >= filter_result.count) {
            is_valid = false;
            return;
        }

        seq_id = filter_result.docs[result_index];
        reference.clear();
        for (auto const& item: filter_result.reference_filter_results) {
            reference[item.first] = item.second[result_index];
        }

        return;
    }

    const filter a_filter = filter_node->filter_exp;

    if (!index->field_is_indexed(a_filter.field_name)) {
        is_valid = false;
        return;
    }

    field f = index->search_schema.at(a_filter.field_name);

    if (f.is_string()) {
        if (filter_node->filter_exp.apply_not_equals) {
            if (id < seq_id) {
                return;
            }

            if (id < result_index) {
                seq_id = id;
                return;
            }

            seq_id = result_index;
            uint32_t previous_match;
            do {
                previous_match = seq_id;
                advance_string_filter_token_iterators();
                doc_matching_string_filter(f.is_array());
            } while (is_valid && previous_match + 1 == seq_id && seq_id >= id);

            if (!is_valid) {
                // filter matched all the ids in the index. So for not equals, there's no match.
                if (previous_match >= index->seq_ids->last_id()) {
                    return;
                }

                is_valid = true;
                seq_id = previous_match + 1;
                result_index = index->seq_ids->last_id() + 1;
                return;
            }

            result_index = seq_id;
            seq_id = previous_match + 1;
            return;
        }

        // Skip all the token iterators and find a new match.
        for (auto& filter_value_tokens : posting_list_iterators) {
            for (auto& token: filter_value_tokens) {
                // We perform AND on tokens. Short-circuiting here.
                if (!token.valid()) {
                    break;
                }

                token.skip_to(id);
            }
        }

        doc_matching_string_filter(f.is_array());
        return;
    }
}

int filter_result_iterator_t::valid(uint32_t id) {
    if (!is_valid) {
        return -1;
    }

    if (filter_node->isOperator) {
        auto left_valid = left_it->valid(id), right_valid = right_it->valid(id);

        if (filter_node->filter_operator == AND) {
            is_valid = left_it->is_valid && right_it->is_valid;

            if (left_valid < 1 || right_valid < 1) {
                if (left_valid == -1 || right_valid == -1) {
                    return -1;
                }

                return 0;
            }

            return 1;
        } else {
            is_valid = left_it->is_valid || right_it->is_valid;

            if (left_valid < 1 && right_valid < 1) {
                if (left_valid == -1 && right_valid == -1) {
                    return -1;
                }

                return 0;
            }

            return 1;
        }
    }

    skip_to(id);
    return is_valid ? (seq_id == id ? 1 : 0) : -1;
}

Option<bool> filter_result_iterator_t::init_status() {
    if (filter_node != nullptr && filter_node->isOperator) {
        auto left_status = left_it->init_status();

        return !left_status.ok() ? left_status : right_it->init_status();
    }

    return status;
}

bool filter_result_iterator_t::contains_atleast_one(const void *obj) {
    if(IS_COMPACT_POSTING(obj)) {
        compact_posting_list_t* list = COMPACT_POSTING_PTR(obj);

        size_t i = 0;
        while(i < list->length && is_valid) {
            size_t num_existing_offsets = list->id_offsets[i];
            size_t existing_id = list->id_offsets[i + num_existing_offsets + 1];

            if (existing_id == seq_id) {
                return true;
            }

            // advance smallest value
            if (existing_id < seq_id) {
                i += num_existing_offsets + 2;
            } else {
                skip_to(existing_id);
            }
        }
    } else {
        auto list = (posting_list_t*)(obj);
        posting_list_t::iterator_t it = list->new_iterator();

        while(it.valid() && is_valid) {
            uint32_t id = it.id();

            if(id == seq_id) {
                return true;
            }

            if(id < seq_id) {
                it.skip_to(seq_id);
            } else {
                skip_to(id);
            }
        }
    }

    return false;
}

void filter_result_iterator_t::reset() {
    if (filter_node == nullptr) {
        return;
    }

    if (filter_node->isOperator) {
        // Reset the subtrees then apply operators to arrive at the first valid doc.
        left_it->reset();
        right_it->reset();
        is_valid = true;

        if (filter_node->filter_operator == AND) {
            and_filter_iterators();
        } else {
            or_filter_iterators();
        }

        return;
    }

    if (is_filter_result_initialized) {
        if (filter_result.count == 0) {
            is_valid = false;
            return;
        }

        result_index = 0;
        seq_id = filter_result.docs[result_index];

        reference.clear();
        for (auto const& item: filter_result.reference_filter_results) {
            reference[item.first] = item.second[result_index];
        }

        is_valid = true;
        return;
    }

    const filter a_filter = filter_node->filter_exp;

    if (!index->field_is_indexed(a_filter.field_name)) {
        return;
    }

    field f = index->search_schema.at(a_filter.field_name);

    if (f.is_string()) {
        posting_list_iterators.clear();
        for(auto expanded_plist: expanded_plists) {
            delete expanded_plist;
        }
        expanded_plists.clear();

        init();
        return;
    }
}

uint32_t filter_result_iterator_t::to_filter_id_array(uint32_t*& filter_array) {
    if (!is_valid) {
        return 0;
    }

    if (is_filter_result_initialized) {
        filter_array = new uint32_t[filter_result.count];
        std::copy(filter_result.docs, filter_result.docs + filter_result.count, filter_array);
        return filter_result.count;
    }

    std::vector<uint32_t> filter_ids;
    do {
        filter_ids.push_back(seq_id);
        next();
    } while (is_valid);

    filter_array = new uint32_t[filter_ids.size()];
    std::copy(filter_ids.begin(), filter_ids.end(), filter_array);

    return filter_ids.size();
}

uint32_t filter_result_iterator_t::and_scalar(const uint32_t* A, const uint32_t& lenA, uint32_t*& results) {
    if (!is_valid) {
        return 0;
    }

    if (is_filter_result_initialized) {
        return ArrayUtils::and_scalar(A, lenA, filter_result.docs, filter_result.count, &results);
    }

    std::vector<uint32_t> filter_ids;
    for (uint32_t i = 0; i < lenA; i++) {
        auto result = valid(A[i]);

        if (result == -1) {
            break;
        }

        if (result == 1) {
            filter_ids.push_back(A[i]);
        }
    }

    if (filter_ids.empty()) {
        return 0;
    }

    results = new uint32_t[filter_ids.size()];
    std::copy(filter_ids.begin(), filter_ids.end(), results);

    return filter_ids.size();
}

filter_result_iterator_t::filter_result_iterator_t(const std::string collection_name, const Index *const index,
                                                   const filter_node_t *const filter_node,
                                                   uint32_t approx_filter_ids_length)  :
        collection_name(collection_name),
        index(index),
        filter_node(filter_node),
        approx_filter_ids_length(approx_filter_ids_length) {
    if (filter_node == nullptr) {
        is_valid = false;
        return;
    }

    // Generate the iterator tree and then initialize each node.
    if (filter_node->isOperator) {
        left_it = new filter_result_iterator_t(collection_name, index, filter_node->left);
        right_it = new filter_result_iterator_t(collection_name, index, filter_node->right);
    }

    init();
}

filter_result_iterator_t::~filter_result_iterator_t() {
    // In case the filter was on string field.
    for(auto expanded_plist: expanded_plists) {
        delete expanded_plist;
    }

    delete left_it;
    delete right_it;
}

filter_result_iterator_t &filter_result_iterator_t::operator=(filter_result_iterator_t &&obj) noexcept {
    if (&obj == this)
        return *this;

    // In case the filter was on string field.
    for(auto expanded_plist: expanded_plists) {
        delete expanded_plist;
    }

    delete left_it;
    delete right_it;

    collection_name = obj.collection_name;
    index = obj.index;
    filter_node = obj.filter_node;
    left_it = obj.left_it;
    right_it = obj.right_it;

    obj.left_it = nullptr;
    obj.right_it = nullptr;

    result_index = obj.result_index;

    filter_result = std::move(obj.filter_result);

    posting_list_iterators = std::move(obj.posting_list_iterators);
    expanded_plists = std::move(obj.expanded_plists);

    is_valid = obj.is_valid;

    seq_id = obj.seq_id;
    reference = std::move(obj.reference);
    status = std::move(obj.status);
    is_filter_result_initialized = obj.is_filter_result_initialized;

    return *this;
}

void filter_result_iterator_t::get_n_ids(const uint32_t& n, std::vector<uint32_t>& results) {
    if (is_filter_result_initialized) {
        for (uint32_t count = 0; count < n && result_index < filter_result.count; count++) {
            results.push_back(filter_result.docs[result_index++]);
        }

        is_valid = result_index < filter_result.count;
        return;
    }

    for (uint32_t count = 0; count < n && is_valid; count++) {
        results.push_back(seq_id);
        next();
    }
}
