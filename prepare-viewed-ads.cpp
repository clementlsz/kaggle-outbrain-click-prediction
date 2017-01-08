#include "util/io.h"
#include "util/data.h"


std::vector<std::pair<std::string, std::string>> filesets {
    std::make_pair("cache/clicks_val_train.csv.gz", "val_train"),
    std::make_pair("cache/clicks_val_test.csv.gz", "val_test"),
    std::make_pair("../input/clicks_train.csv.gz", "full_train"),
    std::make_pair("../input/clicks_test.csv.gz", "full_test"),
};


std::streamsize buffer_size = 1024*1024;

std::pair<int, int> read_event_uid(const std::vector<std::string> & row) {
    return std::make_pair(stoi(row[0]), stoi(row[11]));
}

std::pair<int, int> read_ad_document(const std::vector<std::string> & row) {
    return std::make_pair(stoi(row[0]), stoi(row[1]));
}


std::vector<event> events;
std::vector<ad> ads;
std::unordered_map<int, document> documents;
std::unordered_multimap<int, std::pair<int, float>> document_categories;
std::unordered_multimap<int, std::pair<int, float>> document_topics;


float ctr(int cnt, int pos_cnt) {
    const float reg_n = 10;
    const float reg_p = 0.194;

    return (pos_cnt + reg_p * reg_n) / (cnt + reg_n);
}


template <typename C>
struct counts {
    C past;
    C past_pos;

    C future;
    C future_pos;
};



template <typename C>
class group_writer {
    std::unordered_map<int, counts<C>> grp_counts;
public:
    std::string get_header() {
        return "grp_past_views,grp_past_clicks,grp_future_views,grp_future_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto & cnt = grp_counts[group_id];

        out << int(cnt.past) << ","
            << int(cnt.past_pos) << ","
            << int(cnt.future) << ","
            << int(cnt.future_pos) << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        auto & cnt = grp_counts[group_id];

        if (cnt.past == std::numeric_limits<C>::max())
            throw std::logic_error("Positive overflow");

        ++ cnt.past;

        if (clicked > 0)
            ++ cnt.past_pos;
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        auto & cnt = grp_counts[group_id];

        if (sign > 0 && cnt.future == std::numeric_limits<C>::max())
            throw std::logic_error("Positive overflow");
        else if (sign < 0 && cnt.future == std::numeric_limits<C>::min())
            throw std::logic_error("Negative overflow");

        cnt.future += sign;

        if (clicked > 0)
            cnt.future_pos += sign;
    }
};


template <typename C>
class ad_writer {
    std::unordered_map<std::pair<int, int>, counts<C>> ad_counts;
    std::unordered_map<std::pair<int, int>, counts<C>> ad_doc_counts;
public:
    std::string get_header() {
        return "ad_past_views,ad_past_clicks,ad_doc_past_views,ad_doc_past_clicks,ad_future_views,ad_future_clicks,ad_doc_future_views,ad_doc_future_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;

        auto & ad_cnt = ad_counts[make_pair(group_id, ad_id)];
        auto & ad_doc_cnt = ad_doc_counts[make_pair(group_id, doc_id)];

        out << int(ad_cnt.past) << ","
            << int(ad_cnt.past_pos) << ","
            << int(ad_doc_cnt.past) << ","
            << int(ad_doc_cnt.past_pos) << ","
            << int(ad_cnt.future) << ","
            << int(ad_cnt.future_pos) << ","
            << int(ad_doc_cnt.future) << ","
            << int(ad_doc_cnt.future_pos) << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;

        auto & ad_cnt = ad_counts[make_pair(group_id, ad_id)];
        auto & ad_doc_cnt = ad_doc_counts[make_pair(group_id, doc_id)];

        if (ad_cnt.past == numeric_limits<C>::max() || ad_doc_cnt.past == numeric_limits<C>::max())
            throw std::logic_error("Positive overflow");

        ++ ad_cnt.past;
        ++ ad_doc_cnt.past;

        if (clicked > 0) {
            ++ ad_cnt.past_pos;
            ++ ad_doc_cnt.past_pos;
        }
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;

        auto & ad_cnt = ad_counts[make_pair(group_id, ad_id)];
        auto & ad_doc_cnt = ad_doc_counts[make_pair(group_id, doc_id)];

        if (sign > 0 && (ad_cnt.future == numeric_limits<C>::max() || ad_doc_cnt.future == numeric_limits<C>::max()))
            throw std::logic_error("Positive overflow");
        else if (sign < 0 && (ad_cnt.future == numeric_limits<C>::min() || ad_doc_cnt.future == numeric_limits<C>::min()))
            throw std::logic_error("Negative overflow");

        ad_cnt.future += sign;
        ad_doc_cnt.future += sign;

        if (clicked > 0) {
            ad_cnt.future_pos += sign;
            ad_doc_cnt.future_pos += sign;
        }
    }
};


template <typename C>
class source_writer {
    std::unordered_map<std::pair<int, int>, counts<C>> ad_pub_counts;
    std::unordered_map<std::pair<int, int>, counts<C>> ad_src_counts;
public:
    std::string get_header() {
        return "pub_past_views,pub_past_clicks,src_past_views,src_past_clicks,pub_future_views,pub_future_clicks,src_future_views,src_future_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc = documents.at(doc_id);

        auto & ad_pub_cnt = ad_pub_counts[make_pair(group_id, doc.publisher_id)];
        auto & ad_src_cnt = ad_src_counts[make_pair(group_id, doc.source_id)];

        out << int(ad_pub_cnt.past) << ","
            << int(ad_pub_cnt.past_pos) << ","
            << int(ad_src_cnt.past) << ","
            << int(ad_src_cnt.past_pos) << ","
            << int(ad_pub_cnt.future) << ","
            << int(ad_pub_cnt.future_pos) << ","
            << int(ad_src_cnt.future) << ","
            << int(ad_src_cnt.future_pos) << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc = documents.at(doc_id);

        auto & ad_pub_cnt = ad_pub_counts[make_pair(group_id, doc.publisher_id)];
        auto & ad_src_cnt = ad_src_counts[make_pair(group_id, doc.source_id)];

        if (ad_pub_cnt.past == numeric_limits<C>::max() || ad_src_cnt.past == numeric_limits<C>::max())
            throw std::logic_error("Positive overflow");

        ++ ad_pub_cnt.past;
        ++ ad_src_cnt.past;

        if (clicked > 0) {
            ++ ad_pub_cnt.past_pos;
            ++ ad_src_cnt.past_pos;
        }
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc = documents.at(doc_id);

        auto & ad_pub_cnt = ad_pub_counts[make_pair(group_id, doc.publisher_id)];
        auto & ad_src_cnt = ad_src_counts[make_pair(group_id, doc.source_id)];

        if (sign > 0 && (ad_pub_cnt.future == numeric_limits<C>::max() || ad_src_cnt.future == numeric_limits<C>::max()))
            throw std::logic_error("Positive overflow");
        else if (sign < 0 && (ad_pub_cnt.future == numeric_limits<C>::min() || ad_src_cnt.future == numeric_limits<C>::min()))
            throw std::logic_error("Negative overflow");

        ad_pub_cnt.future += sign;
        ad_src_cnt.future += sign;

        if (clicked > 0) {
            ad_pub_cnt.future_pos += sign;
            ad_src_cnt.future_pos += sign;
        }
    }
};

template <typename C>
class campaign_writer {
    std::unordered_map<std::pair<int, int>, counts<C>> ad_campaign_counts;
    std::unordered_map<std::pair<int, int>, counts<C>> ad_advertiser_counts;
public:
    std::string get_header() {
        return "past_cmp_views,past_cmp_clicks,past_adv_views,past_adv_clicks,future_cmp_views,future_cmp_clicks,future_adv_views,future_adv_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto ad = ads[ad_id];

        auto & ad_cmp_cnt = ad_campaign_counts[make_pair(group_id, ad.campaign_id)];
        auto & ad_adv_cnt = ad_advertiser_counts[make_pair(group_id, ad.advertiser_id)];

        out << int(ad_cmp_cnt.past) << ","
            << int(ad_cmp_cnt.past_pos) << ","
            << int(ad_adv_cnt.past) << ","
            << int(ad_adv_cnt.past_pos) << ","
            << int(ad_cmp_cnt.future) << ","
            << int(ad_cmp_cnt.future_pos) << ","
            << int(ad_adv_cnt.future) << ","
            << int(ad_adv_cnt.future_pos) << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        using namespace std;

        auto ad = ads[ad_id];

        auto & ad_cmp_cnt = ad_campaign_counts[make_pair(group_id, ad.campaign_id)];
        auto & ad_adv_cnt = ad_advertiser_counts[make_pair(group_id, ad.advertiser_id)];

        if (ad_cmp_cnt.past == numeric_limits<C>::max() || ad_adv_cnt.past == numeric_limits<C>::max())
            throw std::logic_error("Positive overflow");

        ++ ad_cmp_cnt.past;
        ++ ad_adv_cnt.past;

        if (clicked > 0) {
            ++ ad_cmp_cnt.past_pos;
            ++ ad_adv_cnt.past_pos;
        }
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        using namespace std;

        auto ad = ads[ad_id];

        auto & ad_cmp_cnt = ad_campaign_counts[make_pair(group_id, ad.campaign_id)];
        auto & ad_adv_cnt = ad_advertiser_counts[make_pair(group_id, ad.advertiser_id)];

        if (sign > 0 && (ad_cmp_cnt.future == numeric_limits<C>::max() || ad_adv_cnt.future == numeric_limits<C>::max()))
            throw std::logic_error("Positive overflow");
        else if (sign < 0 && (ad_cmp_cnt.future == numeric_limits<C>::min() || ad_adv_cnt.future == numeric_limits<C>::min()))
            throw std::logic_error("Negative overflow");

        ad_cmp_cnt.future += sign;
        ad_adv_cnt.future += sign;

        if (clicked > 0) {
            ad_cmp_cnt.future_pos += sign;
            ad_adv_cnt.future_pos += sign;
        }
    }
};

class category_writer {
    std::unordered_map<std::pair<int, int>, counts<float>> ad_cat_counts;
public:
    std::string get_header() {
        return "cat_past_views,cat_past_clicks,cat_future_views,cat_future_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_categories = document_categories.equal_range(doc_id);

        float cat_past_views = 0;
        float cat_past_clicks = 0;
        float cat_future_views = 0;
        float cat_future_clicks = 0;

        for (auto it = doc_categories.first; it != doc_categories.second; ++ it) {
            auto & ad_cat_cnt = ad_cat_counts[make_pair(group_id, it->second.first)];

            cat_past_views += ad_cat_cnt.past * it->second.second;
            cat_past_clicks += ad_cat_cnt.past_pos * it->second.second;

            cat_future_views += ad_cat_cnt.future * it->second.second;
            cat_future_clicks += ad_cat_cnt.future_pos * it->second.second;
        }

        out << cat_past_views << ","
            << cat_past_clicks << ","
            << cat_future_views << ","
            << cat_future_clicks << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_categories = document_categories.equal_range(doc_id);

        for (auto it = doc_categories.first; it != doc_categories.second; ++ it) {
            auto & ad_cat_cnt = ad_cat_counts[make_pair(group_id, it->second.first)];

            ad_cat_cnt.past += it->second.second;

            if (clicked > 0)
                ad_cat_cnt.past_pos += it->second.second;
        }
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_categories = document_categories.equal_range(doc_id);

        for (auto it = doc_categories.first; it != doc_categories.second; ++ it) {
            auto & ad_cat_cnt = ad_cat_counts[make_pair(group_id, it->second.first)];

            ad_cat_cnt.future += it->second.second * sign;

            if (clicked > 0)
                ad_cat_cnt.future_pos += it->second.second * sign;
        }
    }
};

class topic_writer {
    std::unordered_map<std::pair<int, int>, counts<float>> ad_top_counts;
public:
    std::string get_header() {
        return "top_past_views,top_past_clicks,top_future_views,top_future_clicks";
    }

    void write(std::ostream & out, int group_id, int ad_id) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_topics = document_topics.equal_range(doc_id);

        float top_past_views = 0;
        float top_past_clicks = 0;
        float top_future_views = 0;
        float top_future_clicks = 0;

        for (auto it = doc_topics.first; it != doc_topics.second; ++ it) {
            auto & ad_top_cnt = ad_top_counts[make_pair(group_id, it->second.first)];

            top_past_views += ad_top_cnt.past * it->second.second;
            top_past_clicks += ad_top_cnt.past_pos * it->second.second;

            top_future_views += ad_top_cnt.future * it->second.second;
            top_future_clicks += ad_top_cnt.future_pos * it->second.second;
        }

        out << top_past_views << ","
            << top_past_clicks << ","
            << top_future_views << ","
            << top_future_clicks << endl;
    }

    void update_past(int group_id, int ad_id, int clicked) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_topics = document_topics.equal_range(doc_id);

        for (auto it = doc_topics.first; it != doc_topics.second; ++ it) {
            auto & ad_top_cnt = ad_top_counts[make_pair(group_id, it->second.first)];

            ad_top_cnt.past += it->second.second;

            if (clicked > 0)
                ad_top_cnt.past_pos += it->second.second;
        }
    }

    void update_future(int group_id, int ad_id, int clicked, int sign) {
        using namespace std;

        auto doc_id = ads[ad_id].document_id;
        auto doc_topics = document_topics.equal_range(doc_id);

        for (auto it = doc_topics.first; it != doc_topics.second; ++ it) {
            auto & ad_top_cnt = ad_top_counts[make_pair(group_id, it->second.first)];

            ad_top_cnt.future += it->second.second * sign;

            if (clicked > 0)
                ad_top_cnt.future_pos += it->second.second * sign;
        }
    }
};

//////////////////////


struct row {
    int group_id;
    int ad_id;
    int clicked;
};


template <typename W>
void process_group(W & w, const std::vector<row> & group, std::ostream & out, bool update) {
    if (update)
        for (auto it = group.begin(); it != group.end(); ++ it)
            w.update_future(it->group_id, it->ad_id, it->clicked, -1);

    for (auto it = group.begin(); it != group.end(); ++ it)
        w.write(out, it->group_id, it->ad_id);

    if (update)
        for (auto it = group.begin(); it != group.end(); ++ it)
            w.update_past(it->group_id, it->ad_id, it->clicked);
}


template <typename W>
void fill_future(int group_extractor(const event & e), W & w, const std::string & in_file_name) {
    using namespace std;

    compressed_csv_file in(in_file_name);

    cout << "Preparing future for " << in_file_name << "... ";
    cout.flush();

    clock_t begin = clock();

    int i = 0;
    while (true) {
        auto row = in.getrow();

        if (row.empty())
            break;

        w.update_future(group_extractor(events[stoi(row[0])]), stoi(row[1]), stoi(row[2]), 1);

        ++ i;

        if (i % 5000000 == 0) {
            cout << (i / 1000000) << "M... ";
            cout.flush();
        }
    }

    clock_t end = clock();
    double elapsed = double(end - begin) / CLOCKS_PER_SEC;

    cout << "done in " << elapsed << " seconds" << endl;
}


template <typename W>
void generate(int group_extractor(const event & e), const std::string & features_name, uint ofs) {
    using namespace std;

    string a_in_file_name = filesets[ofs].first;
    string b_in_file_name = filesets[ofs+1].first;

    string a_out_file_name = string("cache/") + features_name + string("_") + filesets[ofs].second + string(".csv.gz");
    string b_out_file_name = string("cache/") + features_name + string("_") + filesets[ofs+1].second + string(".csv.gz");

    W w;

    fill_future(group_extractor, w, a_in_file_name);

    cout << "Generating " << a_out_file_name<< " and " << b_out_file_name << "... ";
    cout.flush();

    clock_t begin = clock();

    compressed_csv_file a_in(a_in_file_name);
    compressed_csv_file b_in(b_in_file_name);

    boost::iostreams::filtering_ostream a_out;
    a_out.push(boost::iostreams::gzip_compressor(), buffer_size, buffer_size);
    a_out.push(boost::iostreams::file_sink(a_out_file_name, std::ios_base::out | std::ios_base::binary), buffer_size, buffer_size);
    a_out << w.get_header() << endl;

    boost::iostreams::filtering_ostream b_out;
    b_out.push(boost::iostreams::gzip_compressor(), buffer_size, buffer_size);
    b_out.push(boost::iostreams::file_sink(b_out_file_name, std::ios_base::out | std::ios_base::binary), buffer_size, buffer_size);
    b_out << w.get_header() << endl;

    auto a_row = a_in.getrow();
    auto b_row = b_in.getrow();

    uint i = 0;

    vector<row> group;
    int group_event_id = -1;
    bool group_update = false;
    boost::iostreams::filtering_ostream * group_out = nullptr;

    while (!a_row.empty() || !b_row.empty()) {
        if (b_row.empty() || (!a_row.empty() && stoi(a_row[0]) < stoi(b_row[0]))) {
            int event_id = stoi(a_row[0]);

            row r;
            r.ad_id = stoi(a_row[1]);
            r.group_id = group_extractor(events[event_id]);
            r.clicked = stoi(a_row[2]);

            if (event_id != group_event_id) {
                process_group(w, group, *group_out, group_update);

                group.clear();
                group_event_id = event_id;
                group_out = &a_out;
                group_update = true;
            }

            group.push_back(r);

            a_row = a_in.getrow();
        } else {
            int event_id = stoi(b_row[0]);

            row r;
            r.ad_id = stoi(b_row[1]);
            r.group_id = group_extractor(events[event_id]);
            r.clicked = -1;

            if (event_id != group_event_id) {
                process_group(w, group, *group_out, group_update);

                group.clear();
                group_event_id = event_id;
                group_out = &b_out;
                group_update = false;
            }

            group.push_back(r);

            b_row = b_in.getrow();
        }

        ++ i;

        if (i % 5000000 == 0) {
            cout << (i / 1000000) << "M... ";
            cout.flush();
        }
    }

    process_group(w, group, *group_out, group_update);

    clock_t end = clock();
    double elapsed = double(end - begin) / CLOCKS_PER_SEC;

    cout << "done in " << elapsed << " seconds" << endl;
}


template <typename W>
void generate_all(int group_extractor(const event & e), const std::string & features_name) {
    for (uint ofs = 0; ofs < filesets.size(); ofs += 2)
        generate<W>(group_extractor, features_name, ofs);
}


int uid_extractor(const event & e) {
    return e.uid;
}

std::unordered_map<std::pair<int, std::string>, int> g1_id_map;

// Group by time bucket + platform + location prefix
int g2_extractor(const event & e) {
    int group_time = (e.timestamp / (3600 * 1000 * 3)) % (7 * 8); // Buckets by 3 hours during a week
    auto group = make_pair(e.platform + group_time * 100, e.location.substr(0, 5));

    auto it = g1_id_map.find(group);
    if (it == g1_id_map.end()) {
        int res = g1_id_map.size();
        g1_id_map.insert(std::make_pair(group, res));
        return res;
    } else {
        return it->second;
    }
}


int main() {
    using namespace std;

    cout << "Loading reference data..." << endl;
    events = read_events();
    ads = read_ads();
    documents = read_map("cache/documents.csv.gz", read_document);
    document_categories = read_multi_map("../input/documents_categories.csv.gz", read_document_annotation);
    document_topics = read_multi_map("../input/documents_topics.csv.gz", read_document_annotation);

    // Generating

    generate_all<group_writer<uint16_t>>(uid_extractor, "uid_viewed_grps");
    generate_all<ad_writer<uint8_t>>(uid_extractor, "uid_viewed_ads");
    generate_all<source_writer<uint8_t>>(uid_extractor, "uid_viewed_ad_srcs");
    generate_all<campaign_writer<uint8_t>>(uid_extractor, "uid_viewed_ad_cmps");
    generate_all<category_writer>(uid_extractor, "uid_viewed_ad_cats");
    generate_all<topic_writer>(uid_extractor, "uid_viewed_ad_tops");

    generate_all<ad_writer<uint32_t>>(g2_extractor, "g2_viewed_ads");
    generate_all<source_writer<uint32_t>>(g2_extractor, "g2_viewed_ad_srcs");
    generate_all<campaign_writer<uint32_t>>(g2_extractor, "g2_viewed_ad_cmps");
    generate_all<category_writer>(g2_extractor, "g2_viewed_ad_cats");
    generate_all<topic_writer>(g2_extractor, "g2_viewed_ad_tops");

    cout << "Done." << endl;
}