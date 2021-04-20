#include "stdafx.h"

#pragma warning(push, 0)
#include "resource.h"
#include "foobar2000/helpers/atl-misc.h"
#pragma warning(pop)

#include "config/config_auto.h"
#include "logging.h"
#include "lyric_data.h"
#include "preferences.h"
#include "sources/lyric_source.h"
#include "winstr_util.h"

extern const GUID GUID_PREFERENCES_PAGE_ROOT = { 0x29e96cfa, 0xab67, 0x4793, { 0xa1, 0xc3, 0xef, 0xc3, 0xa, 0xbc, 0x8b, 0x74 } };
static const GUID GUID_CFG_FILENAME_FORMAT = { 0x1f7a3804, 0x7147, 0x4b64, { 0x9d, 0x51, 0x4c, 0xdd, 0x90, 0xa7, 0x6d, 0xd6 } };
static const GUID GUID_CFG_ENABLE_AUTOSAVE = { 0xf25be2d9, 0x4442, 0x4602, { 0xa0, 0xf1, 0x81, 0xd, 0x8e, 0xab, 0x6a, 0x2 } };
static const GUID GUID_CFG_SAVE_METHOD = { 0xdf39b51c, 0xec55, 0x41aa, { 0x93, 0xd3, 0x32, 0xb6, 0xc0, 0x5d, 0x4f, 0xcc } };
static const GUID GUID_CFG_ACTIVE_SOURCES = { 0x7d3c9b2c, 0xb87b, 0x4250, { 0x99, 0x56, 0x8d, 0xf5, 0x80, 0xc9, 0x2f, 0x39 } };

static const GUID GUID_CFG_SAVE_TAG_UNTIMED = { 0x39b0bc08, 0x5c3a, 0x4359, { 0x9d, 0xdb, 0xd4, 0x90, 0x84, 0xb, 0x31, 0x88 } };
static const GUID GUID_CFG_SAVE_TAG_TIMESTAMPED = { 0x337d0d40, 0xe9da, 0x4531, { 0xb0, 0x82, 0x13, 0x24, 0x56, 0xe5, 0xc4, 0x2 } };
static const GUID GUID_CFG_SEARCH_TAGS = { 0xb7332708, 0xe70b, 0x4a6e, { 0xa4, 0xd, 0x14, 0x6d, 0xe3, 0x74, 0x56, 0x65 } };

static cfg_auto_combo_option<SaveMethod> save_method_options[] =
{
    {_T("Don't save"), SaveMethod::None},
    {_T("Save to local file"), SaveMethod::ConfigDirectory},
    {_T("Save to tag"), SaveMethod::Id3Tag},
};

static cfg_auto_bool                 cfg_auto_save_enabled(GUID_CFG_ENABLE_AUTOSAVE, IDC_AUTOSAVE_ENABLED_CHKBOX, true);
static cfg_auto_string               cfg_filename_format(GUID_CFG_FILENAME_FORMAT, IDC_SAVE_FILENAME_FORMAT, "[%artist% - ][%title%]");
static cfg_auto_string               cfg_save_tag_untimed(GUID_CFG_SAVE_TAG_UNTIMED, IDC_SAVE_TAG_UNSYNCED, "UNSYNCEDLYRICS");
static cfg_auto_string               cfg_save_tag_timestamped(GUID_CFG_SAVE_TAG_TIMESTAMPED, IDC_SAVE_TAG_SYNCED, "LYRICS");
static cfg_auto_combo<SaveMethod, 3> cfg_save_method(GUID_CFG_SAVE_METHOD, IDC_SAVE_METHOD_COMBO, SaveMethod::ConfigDirectory, save_method_options);
static cfg_auto_string               cfg_search_tags(GUID_CFG_SEARCH_TAGS, IDC_SEARCH_TAGS, "LYRICS;SYNCEDLYRICS;UNSYNCEDLYRICS;UNSYNCED LYRICS");

static cfg_auto_property* g_root_auto_properties[] =
{
    &cfg_auto_save_enabled,
    &cfg_filename_format,
    &cfg_save_method,
    &cfg_save_tag_untimed,
    &cfg_save_tag_timestamped,
    &cfg_search_tags,
};

// NOTE: This was copied from the localfiles source file.
//       It should not be a problem because these GUIDs must never change anyway (since it would
//       break everybody's config), but probably worth noting that the information is duplicated.
static const GUID localfiles_src_guid = { 0x76d90970, 0x1c98, 0x4fe2, { 0x94, 0x4e, 0xac, 0xe4, 0x93, 0xf3, 0x8e, 0x85 } };

static const GUID cfg_active_sources_default[] = {localfiles_src_guid};
static cfg_objList<GUID> cfg_active_sources(GUID_CFG_ACTIVE_SOURCES, cfg_active_sources_default);

std::vector<GUID> preferences::searching::active_sources()
{
    size_t source_count = cfg_active_sources.get_size();
    std::vector<GUID> result;
    result.reserve(source_count);
    for(size_t i=0; i<source_count; i++)
    {
        result.push_back(cfg_active_sources[i]);
    }
    return result;
}

std::vector<std::string> preferences::searching::tags()
{
    const std::string_view setting = {cfg_search_tags.get_ptr(), cfg_search_tags.get_length()};
    std::vector<std::string> result;

    size_t prev_index = 0;
    for(size_t i=0; i<setting.length(); i++) // Avoid infinite loops
    {
        size_t next_index = setting.find(';', prev_index);
        size_t len = next_index - prev_index;
        if(len > 0)
        {
            result.emplace_back(setting.substr(prev_index, len));
        }

        if((next_index == std::string_view::npos) || (next_index >= setting.length()))
        {
            break;
        }
        prev_index = next_index + 1;
    }
    return result;
}

bool preferences::saving::autosave_enabled()
{
    return cfg_auto_save_enabled.get_value();
}

SaveMethod preferences::saving::save_method()
{
    return cfg_save_method.get_value();
}

const char* preferences::saving::filename_format()
{
    return cfg_filename_format.get_ptr();
}

std::string_view preferences::saving::untimed_tag()
{
    return {cfg_save_tag_untimed.get_ptr(), cfg_save_tag_untimed.get_length()};
}

std::string_view preferences::saving::timestamped_tag()
{
    return {cfg_save_tag_timestamped.get_ptr(), cfg_save_tag_timestamped.get_length()};
}

const LRESULT MAX_SOURCE_NAME_LENGTH = 64;

// The UI for the root element (for OpenLyrics) in the preferences UI tree
class PreferencesRoot : public CDialogImpl<PreferencesRoot>, public auto_preferences_page_instance
{
public:
    // Constructor - invoked by preferences_page_impl helpers - don't do Create() in here, preferences_page_impl does this for us
    PreferencesRoot(preferences_page_callback::ptr callback) : auto_preferences_page_instance(callback, g_root_auto_properties) {}

    // Dialog resource ID - Required by WTL/Create()
    enum {IDD = IDD_PREFERENCES_ROOT};

    void apply() override;
    void reset() override;
    bool has_changed() override;

    BEGIN_MSG_MAP_EX(PreferencesRoot)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_AUTOSAVE_ENABLED_CHKBOX, BN_CLICKED, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_FILENAME_FORMAT, EN_CHANGE, OnSaveNameFormatChange)
        COMMAND_HANDLER_EX(IDC_SEARCH_TAGS, EN_CHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_TAG_SYNCED, EN_CHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_TAG_UNSYNCED, EN_CHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SAVE_METHOD_COMBO, CBN_SELCHANGE, OnUIChange)
        COMMAND_HANDLER_EX(IDC_SOURCE_MOVE_UP_BTN, BN_CLICKED, OnMoveUp)
        COMMAND_HANDLER_EX(IDC_SOURCE_MOVE_DOWN_BTN, BN_CLICKED, OnMoveDown)
        COMMAND_HANDLER_EX(IDC_SOURCE_ACTIVATE_BTN, BN_CLICKED, OnSourceActivate)
        COMMAND_HANDLER_EX(IDC_SOURCE_DEACTIVATE_BTN, BN_CLICKED, OnSourceDeactivate)
        COMMAND_HANDLER_EX(IDC_ACTIVE_SOURCE_LIST, LBN_SELCHANGE, OnActiveSourceSelect)
        COMMAND_HANDLER_EX(IDC_INACTIVE_SOURCE_LIST, LBN_SELCHANGE, OnInactiveSourceSelect)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM);
    void OnUIChange(UINT, int, CWindow);
    void OnSaveNameFormatChange(UINT, int, CWindow);
    void OnMoveUp(UINT, int, CWindow);
    void OnMoveDown(UINT, int, CWindow);
    void OnSourceActivate(UINT, int, CWindow);
    void OnSourceDeactivate(UINT, int, CWindow);
    void OnActiveSourceSelect(UINT, int, CWindow);
    void OnInactiveSourceSelect(UINT, int, CWindow);

    void SourceListInitialise();
    void SourceListResetFromSaved();
    void SourceListResetToDefault();
    void SourceListApply();
    bool SourceListHasChanged();
};

BOOL PreferencesRoot::OnInitDialog(CWindow, LPARAM)
{
    SourceListInitialise();
    init_auto_preferences();
    return FALSE;
}

void PreferencesRoot::OnUIChange(UINT, int, CWindow)
{
    on_ui_interaction();
}

void PreferencesRoot::OnSaveNameFormatChange(UINT, int, CWindow)
{
    CWindow preview_item = GetDlgItem(IDC_FILE_NAME_PREVIEW);
    assert(preview_item != nullptr);

    LRESULT format_text_length_result = SendDlgItemMessage(IDC_SAVE_FILENAME_FORMAT, WM_GETTEXTLENGTH, 0, 0);
    if(format_text_length_result > 0)
    {
        size_t format_text_length = (size_t)format_text_length_result;
        TCHAR* format_text_buffer = new TCHAR[format_text_length+1]; // +1 for null-terminator
        GetDlgItemText(IDC_SAVE_FILENAME_FORMAT, format_text_buffer, format_text_length+1);
        std::string format_text = from_tstring(std::tstring_view{format_text_buffer, format_text_length});
        delete[] format_text_buffer;

        titleformat_object::ptr format_script;
        bool compile_success = titleformat_compiler::get()->compile(format_script, format_text.c_str());
        if(compile_success)
        {
            metadb_handle_ptr preview_track = nullptr;

            service_ptr_t<playback_control> playback = playback_control::get();
            if(playback->get_now_playing(preview_track))
            {
                LOG_INFO("Playback is currently active, using the now-playing track for format preview");
            }
            else
            {
                pfc::list_t<metadb_handle_ptr> selection;

                service_ptr_t<playlist_manager> playlist = playlist_manager::get();
	            playlist->activeplaylist_get_selected_items(selection);

                if(selection.get_count() > 0)
                {
                    LOG_INFO("Using the first selected item for format preview");
                    preview_track = selection[0];
                }
                else if(playlist->activeplaylist_get_item_handle(preview_track, 0))
                {
                    LOG_INFO("No selection available, using the first playlist item for format preview");
                }
                else
                {
                    LOG_INFO("No selection available & no active playlist. There will be no format preview");
                }
            }

            if(preview_track != nullptr)
            {
                pfc::string8 formatted_title;
                bool format_success = preview_track->format_title(nullptr, formatted_title, format_script, nullptr); 
                if(format_success)
                {
                    formatted_title.fix_filename_chars();

                    std::tstring preview = to_tstring(formatted_title);
                    preview_item.SetWindowText(preview.c_str());
                }
                else
                {
                    preview_item.SetWindowText(_T("<Unexpected formatting error>"));
                }
            }
            else
            {
                preview_item.SetWindowText(_T(""));
            }
        }
        else
        {
            preview_item.SetWindowText(_T("<Invalid format>"));
        }
    }
    else
    {
        preview_item.SetWindowText(_T(""));
    }

    on_ui_interaction();
}

void PreferencesRoot::OnMoveUp(UINT, int, CWindow)
{
    LRESULT select_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCURSEL, 0, 0);
    if(select_index == LB_ERR)
    {
        return; // No selection
    }
    if(select_index == 0)
    {
        return; // Can't move the top item upwards
    }

    TCHAR buffer[MAX_SOURCE_NAME_LENGTH];
    LRESULT select_strlen = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXTLEN, select_index, 0);
    assert(select_strlen+1 <= MAX_SOURCE_NAME_LENGTH);

    LRESULT strcopy_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXT, select_index, (LPARAM)buffer);
    LRESULT select_data = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETITEMDATA, select_index, 0);
    LRESULT delete_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_DELETESTRING, select_index, 0);
    assert(strcopy_result != LB_ERR);
    assert(select_data != LB_ERR);
    assert(delete_result != LB_ERR);

    LRESULT new_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_INSERTSTRING, select_index-1, (LPARAM)buffer);
    LRESULT set_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, select_data);
    LRESULT select_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETCURSEL, new_index, 0);
    assert((new_index != LB_ERR) && (new_index != LB_ERRSPACE));
    assert(set_result != LB_ERR);
    assert(select_result != LB_ERR);

    OnActiveSourceSelect(0, 0, {}); // Update the button enabled state (e.g if we moved an item to the bottom we should disable the "down" button)

    on_ui_interaction();
}

void PreferencesRoot::OnMoveDown(UINT, int, CWindow)
{
    LRESULT item_count = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCOUNT, 0, 0);
    assert(item_count != LB_ERR);

    LRESULT select_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCURSEL, 0, 0);
    if(select_index == LB_ERR)
    {
        return; // No selection
    }
    if(select_index+1 == item_count)
    {
        return; // Can't move the bottom item downwards
    }

    TCHAR buffer[MAX_SOURCE_NAME_LENGTH];
    LRESULT select_strlen = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXTLEN, select_index, 0);
    assert(select_strlen+1 <= MAX_SOURCE_NAME_LENGTH);

    LRESULT strcopy_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXT, select_index, (LPARAM)buffer);
    LRESULT select_data = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETITEMDATA, select_index, 0);
    LRESULT delete_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_DELETESTRING, select_index, 0);
    assert(strcopy_result != LB_ERR);
    assert(select_data != LB_ERR);
    assert(delete_result != LB_ERR);

    LRESULT new_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_INSERTSTRING, select_index+1, (LPARAM)buffer);
    LRESULT set_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, select_data);
    LRESULT select_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETCURSEL, new_index, 0);
    assert((new_index != LB_ERR) && (new_index != LB_ERRSPACE));
    assert(set_result != LB_ERR);
    assert(select_result != LB_ERR);

    OnActiveSourceSelect(0, 0, {}); // Update the button enabled state (e.g if we moved an item to the bottom we should disable the "down" button)

    on_ui_interaction();
}

void PreferencesRoot::OnSourceActivate(UINT, int, CWindow)
{
    LRESULT select_index = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_GETCURSEL, 0, 0);
    if(select_index == LB_ERR)
    {
        return; // No selection
    }

    TCHAR buffer[MAX_SOURCE_NAME_LENGTH];
    LRESULT select_strlen = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXTLEN, select_index, 0);
    assert(select_strlen+1 <= MAX_SOURCE_NAME_LENGTH);

    LRESULT strcopy_result = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_GETTEXT, select_index, (LPARAM)buffer);
    LRESULT select_data = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_GETITEMDATA, select_index, 0);
    LRESULT delete_result = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_DELETESTRING, select_index, 0);
    assert(strcopy_result != LB_ERR);
    assert(select_data != LB_ERR);
    assert(delete_result != LB_ERR);

    LRESULT new_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)buffer);
    LRESULT set_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, select_data);
    assert((new_index != LB_ERR) && (new_index != LB_ERRSPACE));
    assert(set_result != LB_ERR);

    on_ui_interaction();
}

void PreferencesRoot::OnSourceDeactivate(UINT, int, CWindow)
{
    LRESULT select_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCURSEL, 0, 0);
    if(select_index == LB_ERR)
    {
        return; // No selection
    }

    TCHAR buffer[MAX_SOURCE_NAME_LENGTH];
    LRESULT select_strlen = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXTLEN, select_index, 0);
    assert(select_strlen+1 <= MAX_SOURCE_NAME_LENGTH);

    LRESULT strcopy_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETTEXT, select_index, (LPARAM)buffer);
    LRESULT select_data = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETITEMDATA, select_index, 0);
    LRESULT delete_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_DELETESTRING, select_index, 0);
    assert(strcopy_result != LB_ERR);
    assert(select_data != LB_ERR);
    assert(delete_result != LB_ERR);

    LRESULT new_index = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)buffer);
    LRESULT set_result = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, select_data);
    assert((new_index != LB_ERR) && (new_index != LB_ERRSPACE));
    assert(set_result != LB_ERR);

    on_ui_interaction();
}

void PreferencesRoot::OnActiveSourceSelect(UINT, int, CWindow)
{
    LRESULT select_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCURSEL, 0, 0);
    LRESULT item_count = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCOUNT, 0, 0);
    assert(item_count != LB_ERR);

    SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_SETCURSEL, -1);

    CWindow activate_btn = GetDlgItem(IDC_SOURCE_ACTIVATE_BTN);
    CWindow deactivate_btn = GetDlgItem(IDC_SOURCE_DEACTIVATE_BTN);
    assert(activate_btn != nullptr);
    assert(deactivate_btn != nullptr);
    activate_btn.EnableWindow(FALSE);
    deactivate_btn.EnableWindow(TRUE);

    CWindow move_up_btn = GetDlgItem(IDC_SOURCE_MOVE_UP_BTN);
    assert(move_up_btn != nullptr);
    move_up_btn.EnableWindow((select_index != LB_ERR) && (select_index != 0));

    CWindow move_down_btn = GetDlgItem(IDC_SOURCE_MOVE_DOWN_BTN);
    assert(move_down_btn != nullptr);
    move_down_btn.EnableWindow((select_index != LB_ERR) && (select_index+1 != item_count));
}

void PreferencesRoot::OnInactiveSourceSelect(UINT, int, CWindow)
{
    SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETCURSEL, -1);

    CWindow activate_btn = GetDlgItem(IDC_SOURCE_ACTIVATE_BTN);
    CWindow deactivate_btn = GetDlgItem(IDC_SOURCE_DEACTIVATE_BTN);
    assert(activate_btn != nullptr);
    assert(deactivate_btn != nullptr);
    activate_btn.EnableWindow(TRUE);
    deactivate_btn.EnableWindow(FALSE);

    CWindow move_up_btn = GetDlgItem(IDC_SOURCE_MOVE_UP_BTN);
    CWindow move_down_btn = GetDlgItem(IDC_SOURCE_MOVE_DOWN_BTN);
    assert(move_up_btn != nullptr);
    assert(move_down_btn != nullptr);
    move_up_btn.EnableWindow(FALSE);
    move_down_btn.EnableWindow(FALSE);
}

void PreferencesRoot::reset()
{
    SourceListResetToDefault();
    auto_preferences_page_instance::reset();
}

void PreferencesRoot::apply()
{
    SourceListApply();
    auto_preferences_page_instance::apply();
}

bool PreferencesRoot::has_changed()
{
    if(SourceListHasChanged()) return true;
    return auto_preferences_page_instance::has_changed();
}

void PreferencesRoot::SourceListInitialise()
{
    SourceListResetFromSaved();
}

void PreferencesRoot::SourceListResetFromSaved()
{
    SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_RESETCONTENT, 0, 0);

    std::vector<GUID> all_src_ids = LyricSourceBase::get_all_ids();
    size_t total_source_count = all_src_ids.size();
    std::vector<bool> sources_active(total_source_count);

    size_t active_source_count = cfg_active_sources.get_count();
    for(size_t active_source_index=0; active_source_index<active_source_count; active_source_index++)
    {
        GUID src_guid = cfg_active_sources[active_source_index];
        LyricSourceBase* src = LyricSourceBase::get(src_guid);
        assert(src != nullptr);

        bool found = false;
        for(size_t i=0; i<total_source_count; i++)
        {
            if(all_src_ids[i] == src_guid)
            {
                sources_active[i] = true;
                found = true;
                break;
            }
        }
        assert(found);

        LRESULT new_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)src->friendly_name().data());
        LRESULT set_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, (LPARAM)&src->id());
        assert(new_index != LB_ERR);
        assert(set_result != LB_ERR);
    }

    for(size_t entry_index=0; entry_index<total_source_count; entry_index++)
    {
        if(sources_active[entry_index]) continue;

        LyricSourceBase* src = LyricSourceBase::get(all_src_ids[entry_index]);
        assert(src != nullptr);

        LRESULT new_index = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)src->friendly_name().data());
        LRESULT set_result = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, (LPARAM)&src->id());
        assert(new_index != LB_ERR);
        assert(set_result != LB_ERR);
    }
}

void PreferencesRoot::SourceListResetToDefault()
{
    SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_RESETCONTENT, 0, 0);

    std::vector<GUID> all_src_ids = LyricSourceBase::get_all_ids();
    size_t total_source_count = all_src_ids.size();
    std::vector<bool> sources_active(total_source_count);

    for(GUID src_guid : cfg_active_sources_default)
    {
        LyricSourceBase* src = LyricSourceBase::get(src_guid);
        assert(src != nullptr);

        bool found = false;
        for(size_t i=0; i<total_source_count; i++)
        {
            if(all_src_ids[i] == src_guid)
            {
                sources_active[i] = true;
                found = true;
                break;
            }
        }
        assert(found);

        LRESULT new_index = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)src->friendly_name().data());
        LRESULT set_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, (LPARAM)&src->id());
        assert(new_index != LB_ERR);
        assert(set_result != LB_ERR);
    }

    for(size_t entry_index=0; entry_index<total_source_count; entry_index++)
    {
        if(sources_active[entry_index]) continue;

        LyricSourceBase* src = LyricSourceBase::get(all_src_ids[entry_index]);
        assert(src != nullptr);

        LRESULT new_index = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_ADDSTRING, 0, (LPARAM)src->friendly_name().data());
        LRESULT set_result = SendDlgItemMessage(IDC_INACTIVE_SOURCE_LIST, LB_SETITEMDATA, new_index, (LPARAM)&src->id());
        assert(new_index != LB_ERR);
        assert(set_result != LB_ERR);
    }
}

void PreferencesRoot::SourceListApply()
{
    cfg_active_sources.remove_all();

    LRESULT item_count = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCOUNT, 0, 0);
    assert(item_count != LB_ERR);

    for(LRESULT item_index=0; item_index<item_count; item_index++)
    {
        LRESULT item_data = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETITEMDATA, item_index, 0);
        assert(item_data != LB_ERR);
        const GUID* ui_item_id = (GUID*)item_data;
        assert(ui_item_id != nullptr);

        cfg_active_sources.add_item(*ui_item_id);
    }
}

bool PreferencesRoot::SourceListHasChanged()
{
    size_t saved_item_count = cfg_active_sources.get_count();
    LRESULT ui_item_count_result = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETCOUNT, 0, 0);
    assert(ui_item_count_result != LB_ERR);
    assert(ui_item_count_result >= 0);
    size_t ui_item_count = static_cast<size_t>(ui_item_count_result);

    if(saved_item_count != ui_item_count)
    {
        return true;
    }
    assert(saved_item_count == ui_item_count);

    for(size_t item_index=0; item_index<saved_item_count; item_index++)
    {
        LRESULT ui_item = SendDlgItemMessage(IDC_ACTIVE_SOURCE_LIST, LB_GETITEMDATA, item_index, 0);
        assert(ui_item != LB_ERR);

        const GUID* ui_item_id = (const GUID*)ui_item;
        assert(ui_item_id != nullptr);

        GUID saved_item_id = cfg_active_sources[item_index];

        if(saved_item_id != *ui_item_id)
        {
            return true;
        }
    }

    return false;
}

class PreferencesRootImpl : public preferences_page_impl<PreferencesRoot>
{
public:
    const char* get_name() { return "OpenLyrics"; }
    GUID get_guid() { return GUID_PREFERENCES_PAGE_ROOT; }
    GUID get_parent_guid() { return guid_tools; }
};

static preferences_page_factory_t<PreferencesRootImpl> g_preferences_page_root_factory;
