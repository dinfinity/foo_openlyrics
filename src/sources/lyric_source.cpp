#include "stdafx.h"

#include "logging.h"
#include "lyric_source.h"
#include "tag_util.h"

static std::vector<LyricSourceBase*> g_lyric_sources;

LyricSourceBase* LyricSourceBase::get(GUID id)
{
    for(LyricSourceBase* src : g_lyric_sources)
    {
        if(src->id() == id)
        {
            return src;
        }
    }

    return nullptr;
}

std::vector<GUID> LyricSourceBase::get_all_ids()
{
    std::vector<GUID> result;
    result.reserve(g_lyric_sources.size());
    for(LyricSourceBase* src : g_lyric_sources)
    {
        result.push_back(src->id());
    }
    return result;
}

void LyricSourceBase::on_init()
{
    g_lyric_sources.push_back(this);
}

std::string LyricSourceBase::urlencode(std::string_view input)
{
    size_t inlen = input.length();
    std::string result;
    result.reserve(inlen * 3);

    for(size_t i=0; i<inlen; i++)
    {
        if(pfc::char_is_ascii_alphanumeric(input[i]) ||
            (input[i] == '-') ||
            (input[i] == '_') ||
            (input[i] == '.') ||
            (input[i] == '~'))
        {
            result += input[i];
        }
        else if(input[i] == ' ')
        {
            result += "%20";
        }
        else
        {
            const auto nibble_to_hex = [](char c)
            {
                static char hex[] = "0123456789ABCDEF";
                return hex[c & 0xF];
            };

            char hi_nibble = ((input[i] >> 4) & 0xF);
            char lo_nibble = ((input[i] >> 0) & 0xF);

            result += '%';
            result += nibble_to_hex(hi_nibble);
            result += nibble_to_hex(lo_nibble);
        }
    }

    return result;
}

bool LyricSourceRemote::is_local() const
{
    return false;
}

std::vector<LyricDataRaw> LyricSourceRemote::search(metadb_handle_ptr track, abort_callback& abort)
{
    std::string artist = track_metadata(track, "artist");
    std::string album = track_metadata(track, "album");
    std::string title = track_metadata(track, "title");

    if(preferences::searching::exclude_trailing_brackets())
    {
        artist = trim_surrounding_whitespace(trim_trailing_text_in_brackets(artist));
        album = trim_surrounding_whitespace(trim_trailing_text_in_brackets(album));
        title = trim_surrounding_whitespace(trim_trailing_text_in_brackets(title));
    }

    return search(artist, album, title, abort);
}

std::string LyricSourceRemote::save(metadb_handle_ptr /*track*/, bool /*is_timestamped*/, std::string_view /*lyrics*/, bool /*allow_ovewrite*/, abort_callback& /*abort*/)
{
    LOG_WARN("Cannot save lyrics to a remote source");
    assert(false);
    return "";
}
