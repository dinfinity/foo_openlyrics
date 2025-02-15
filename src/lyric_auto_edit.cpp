#include "stdafx.h"

#include "parsers.h"
#include "logging.h"
#include "lyric_auto_edit.h"
#include "lyric_io.h"

std::optional<LyricData> auto_edit::RunAutoEdit(AutoEditType type, const LyricData& lyrics)
{
    switch(type)
    {
        case AutoEditType::CreateInstrumental: return CreateInstrumental(lyrics);
        case AutoEditType::ReplaceHtmlEscapedChars: return ReplaceHtmlEscapedChars(lyrics);
        case AutoEditType::RemoveRepeatedSpaces: return RemoveRepeatedSpaces(lyrics);
        case AutoEditType::RemoveRepeatedBlankLines: return RemoveRepeatedBlankLines(lyrics);
        case AutoEditType::RemoveAllBlankLines: return RemoveAllBlankLines(lyrics);
        case AutoEditType::ResetCapitalisation: return ResetCapitalisation(lyrics);
        case AutoEditType::FixMalformedTimestamps: return FixMalformedTimestamps(lyrics);

        case AutoEditType::Unknown:
        default:
            LOG_ERROR("Unexpected auto-edit type: %d", int(type));
            assert(false);
            return {};
    }
}

std::optional<LyricData> auto_edit::CreateInstrumental(const LyricData& /*lyrics*/)
{
    LyricData lyrics;
    lyrics.lines.push_back({_T("[Instrumental]"), DBL_MAX});
    lyrics.text = "[Instrumental]";
    return {std::move(lyrics)};
}

std::optional<LyricData> auto_edit::ReplaceHtmlEscapedChars(const LyricData& lyrics)
{
    LyricDataRaw new_raw = lyrics;
    std::pair<std::string_view, char> replacements[] =
    {
        {"&amp;", '&'},
        {"&lt;", '<'},
        {"&gt;", '>'},
        {"&quot;", '"'},
        {"&apos;", '\''},
    };

    size_t replace_count = 0;
    for(auto [escaped, replacement] : replacements)
    {
        size_t current_index = 0;
        while(current_index < new_raw.text.length())
        {
            size_t next_index = new_raw.text.find(escaped, current_index);
            if(next_index == std::string::npos) break;

            new_raw.text.replace(next_index, escaped.length(), 1, replacement);
            current_index = next_index + 1;
            replace_count++;
        }
    }
    LOG_INFO("Auto-edit replaced %u named HTML-encoded characters", replace_count);

    if(replace_count > 0)
    {
        LyricData new_lyrics = parsers::lrc::parse(new_raw);
        return {std::move(new_lyrics)};
    }
    else
    {
        return {};
    }
}

std::optional<LyricData> auto_edit::RemoveRepeatedSpaces(const LyricData& lyrics)
{
    size_t spaces_erased = 0;
    LyricData new_lyrics = lyrics;
    for(LyricDataLine& line : new_lyrics.lines)
    {
        size_t search_start = 0;
        while(search_start < line.text.length())
        {
            size_t next_space = line.text.find_first_of(_T(' '), search_start);

            // NOTE: If the line was empty we would not enter this loop.
            //       We subtract 1 from the length to avoid overflowing when next_space == npos == (size_t)-1
            assert(line.text.length() > 0);
            if(next_space > line.text.length()-1)
            {
                break;
            }

            size_t erase_start = next_space + 1;
            size_t erase_end = line.text.find_first_not_of(_T(' '), erase_start);

            if((erase_end != std::tstring::npos) && (erase_end > erase_start))
            {
                size_t erase_count = erase_end - erase_start;
                line.text.erase(erase_start, erase_count);
                spaces_erased += erase_count;
            }
            search_start = next_space + 1;
        }
    }
    LOG_INFO("Auto-removal removed %u unnecessary spaces", spaces_erased);

    if(spaces_erased > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        return {std::move(new_lyrics)};
    }
    else
    {
        return {};
    }
}

std::optional<LyricData> auto_edit::RemoveRepeatedBlankLines(const LyricData& lyrics)
{
    size_t lines_removed = 0;
    bool previous_blank = true;
    LyricData new_lyrics = lyrics;
    for(auto iter=new_lyrics.lines.begin(); iter != new_lyrics.lines.end(); /*Omitted*/)
    {
        size_t first_non_space = iter->text.find_first_not_of(' ');
        bool is_blank = (first_non_space == std::tstring::npos);
        if(is_blank && previous_blank)
        {
            iter = new_lyrics.lines.erase(iter);
            lines_removed++;
        }
        else
        {
            iter++;
        }
        previous_blank = is_blank;
    }
    LOG_INFO("Auto-removal removed %u blank lines", lines_removed);

    if(lines_removed > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        return {std::move(new_lyrics)};
    }
    else
    {
        return {};
    }
}

std::optional<LyricData> auto_edit::RemoveAllBlankLines(const LyricData& lyrics)
{
    LyricData new_lyrics = lyrics;
    auto line_is_empty = [](const LyricDataLine& line) { return line.text.find_first_not_of(' ') == std::tstring::npos; };
    auto new_end = std::remove_if(new_lyrics.lines.begin(), new_lyrics.lines.end(), line_is_empty);
    int lines_removed = std::distance(new_end, new_lyrics.lines.end());
    new_lyrics.lines.erase(new_end, new_lyrics.lines.end());
    LOG_INFO("Auto-removal removed %d blank lines", lines_removed);

    if(lines_removed > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        return {std::move(new_lyrics)};
    }
    else
    {
        return {};
    }
}

std::optional<LyricData> auto_edit::ResetCapitalisation(const LyricData& lyrics)
{
    LyricData new_lyrics = lyrics;

    size_t edit_count = 0;
    for(LyricDataLine& line : new_lyrics.lines)
    {
        if(line.text.empty()) continue;

        bool edited = false;
        if(line.text[0] <= 255)
        {
            if(_istlower(line.text[0]))
            {
                line.text[0] = _totupper(static_cast<unsigned char>(line.text[0]));
                edited = true;
            }
        }

        for(size_t i=1; i<line.text.length(); i++)
        {
            if(line.text[i] <= 255)
            {
                if(_istupper(line.text[i]))
                {
                    line.text[i] = _totlower(static_cast<unsigned char>(line.text[i]));
                    edited = true;
                }
            }
        }

        if(edited)
        {
            edit_count++;
        }
    }
    LOG_INFO("Auto-edit changed the capitalisation of %u lines", edit_count);

    if(edit_count > 0)
    {
        new_lyrics.text = parsers::lrc::shrink_text(new_lyrics);
        return {std::move(new_lyrics)};
    }
    else
    {
        return {};
    }
}

std::optional<LyricData> auto_edit::FixMalformedTimestamps(const LyricData& lyrics)
{
    const auto is_digit = [](char c)
    {
        return ((c >= '0') && (c <= '9'));
    };

    const auto fix_too_many_decimal_places = [&is_digit](std::string& line, size_t start)
    {
        if((line.length() <= start + 10) ||
           (line[start+0] != '[') ||
           !is_digit(line[start+1]) ||
           !is_digit(line[start+2]) ||
           (line[start+3] != ':') ||
           !is_digit(line[start+4]) ||
           !is_digit(line[start+5]) ||
           (line[start+6] != '.') ||
           !is_digit(line[start+7]) ||
           !is_digit(line[start+8]))
        {
            return false;
        }

        size_t erase_start = start + 9;
        size_t tag_end_index = line.find(']', erase_start);
        if(tag_end_index == std::tstring::npos)
        {
            // Unclosed timestamp tag, not what we're fixing here
            return false;
        }

        assert(tag_end_index >= erase_start);
        size_t erase_count = tag_end_index - erase_start;
        if(erase_count == 0)
        {
            return false;
        }

        line.erase(erase_start, erase_count);
        return true;
    };

    const auto fix_decimal_separator = [&is_digit](std::string& line, size_t start)
    {
        if((line.length() < start + 10) ||
           (line[start+0] != '[') ||
           !is_digit(line[start+1]) ||
           !is_digit(line[start+2]) ||
           (line[start+3] != ':') ||
           !is_digit(line[start+4]) ||
           !is_digit(line[start+5]) ||
           (line[start+6] == '.') || // Already correct
           !is_digit(line[start+7]) ||
           !is_digit(line[start+8]) ||
           (line.find(']', start+9) == std::tstring::npos)) // Unclosed timestamp tag, not what we're fixing here
        {
            return false;
        }

        line[start+6] = '.';
        return true;
    };

    LyricDataRaw new_lyrics = lyrics;

    int change_count = 0;
    size_t current_index = new_lyrics.text.find('[');
    while(current_index != std::string::npos)
    {
        bool changed = false;
        changed |= fix_decimal_separator(new_lyrics.text, current_index);
        changed |= fix_too_many_decimal_places(new_lyrics.text, current_index);

        if(changed)
        {
            change_count++;
        }
        current_index = new_lyrics.text.find('[', current_index+1);
    }

    if(change_count > 0)
    {
        return {parsers::lrc::parse(new_lyrics)};
    }
    else
    {
        return {};
    }
}
