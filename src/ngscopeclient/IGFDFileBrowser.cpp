/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of IGFDFileBrowser
 */
#include "ngscopeclient.h"
#include "IGFDFileBrowser.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

IGFDFileBrowser::IGFDFileBrowser(
	const string& initialPath,
	const string& title,
	const string& id,
	const vector<FileBrowserFilter>& filters,
	bool saveDialog
	)
	: m_closed(false)
	, m_closedOK(false)
	, m_filters(filters)
	, m_saveDialog(saveDialog)
	, m_id(id)
{
	//If linux read ~/.config/gtk-3.0/bookmarks
	//TODO: read bookmarks on other OSes
	#ifdef __linux__
		string home;
		auto phome = getenv("HOME");
		if(phome)
			home = phome;
		string path = home + "/.config/gtk-3.0/bookmarks";
		FILE* fp = fopen(path.c_str(), "r");
		if(fp)
		{
			char line[1024];
			char fname[512] = "";
			char bname[512] = "";
			while(fgets(line, sizeof(line), fp) != nullptr)
			{
				auto sline = Trim(line);
				auto nfields = sscanf(sline.c_str(), "file://%511[^ ] %511s", fname, bname);
				if(nfields == 2)
					m_bookmarks[fname] = bname;
				else if(nfields == 1)
					m_bookmarks[fname] = BaseName(fname);
			}
			fclose(fp);
		}
	#endif

	//Tweak the mask for imgui filedialog: Name{.ext1,.ext2},Name2{.ext3}
	//(needs to be in parentheses to be recognized as a regex)
	//Special case for touchstone since internal parentheses aren't well supported by IGFD
	string mask;
	for(auto& f : filters)
	{
		if(!mask.empty())
			mask += ",";
		if(f.mask == "*.s*p")
			mask += "Touchstone files (*.s*p){.s2p,.s3p,.s4p,.s5p,.s6p,.s7p,.s8p,.s9p,.snp}";
		else
		{
			string exts;
			for(auto& pattern : SplitFileBrowserMask(f.mask))
			{
				if(!exts.empty())
					exts += ",";
				exts += pattern.substr(1);
			}
			mask += f.name + "{" + exts + "}";
		}
	}

	for(auto jt : m_bookmarks)
		m_dialog.AddBookmark(jt.second, jt.first);
	if(saveDialog)
	{
		m_dialog.OpenDialog(
			m_id,
			title,
			mask.c_str(),
			".",
			initialPath,
			ImGuiFileDialogFlags_ConfirmOverwrite);
	}
	else
	{
		m_dialog.OpenDialog(
			m_id,
			title,
			mask.c_str(),
			".",
			initialPath);
	}
}

IGFDFileBrowser::~IGFDFileBrowser()
{
	//TODO: save bookmarks at exit
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI handlers

void IGFDFileBrowser::Render()
{
	if(m_closed)
		return;

	float fontsize = ImGui::GetFontSize();
	if(m_dialog.Display(m_id, ImGuiWindowFlags_NoCollapse, ImVec2(60*fontsize, 30*fontsize)))
	{
		if(m_dialog.IsOk())
			m_closedOK = true;
		m_closed = true;
	}
}

bool IGFDFileBrowser::IsClosed()
{
	return m_closed;
}

bool IGFDFileBrowser::IsClosedOK()
{
	return m_closedOK;
}

string IGFDFileBrowser::GetFileName()
{
	string path = m_dialog.GetFilePathName();
	if(!m_saveDialog)
		return path;

	//IGFD only adds the extension of the selected file type for simple filters, not for the "Name{.ext}" ones we use.
	//So add it ourselves, unless the name already has one of the extensions offered (so typing an extension picks
	//the file type, like in the native dialogs)
	auto hasExtension = [&path](const string& pattern)
	{
		//Plain "*.ext" patterns only
		if( (pattern.length() < 3) || (pattern.compare(0, 2, "*.") != 0) || (pattern.find('*', 1) != string::npos) )
			return false;
		string ext = pattern.substr(1);
		if(path.length() < ext.length())
			return false;
		for(size_t i=0; i<ext.length(); i++)
		{
			auto c = static_cast<unsigned char>(path[path.length() - ext.length() + i]);
			if(tolower(c) != tolower(static_cast<unsigned char>(ext[i])))
				return false;
		}
		return true;
	};
	for(auto& f : m_filters)
	{
		for(auto& pattern : SplitFileBrowserMask(f.mask))
		{
			if(hasExtension(pattern))
				return path;
		}
	}

	auto current = m_dialog.GetCurrentFilter();
	for(auto& f : m_filters)
	{
		if(f.name != current)
			continue;
		auto patterns = SplitFileBrowserMask(f.mask);
		if(!patterns.empty() && (patterns[0].compare(0, 2, "*.") == 0) && (patterns[0].find('*', 1) == string::npos))
			path += patterns[0].substr(1);
		break;
	}
	return path;
}
