/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of SessionWriter, SessionReader and their implementations
 */
#include "ngscopeclient.h"
#include "SessionStorage.h"

#include <miniz.h>

#include <filesystem>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

static const char* const g_sessionExtension = ".scopesession";
static const char* const g_archiveExtension = ".scopearchive";
static const char* const g_configExtension = ".scopeconfig";

static bool EndsWith(const string& str, const string& suffix)
{
	return (str.length() >= suffix.length()) && (str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0);
}

///@brief Case insensitive version of EndsWith(), for checking file extensions (suffix must be lower case)
static bool EndsWithNoCase(const string& str, const string& suffix)
{
	if(str.length() < suffix.length())
		return false;
	size_t start = str.length() - suffix.length();
	for(size_t i=0; i<suffix.length(); i++)
	{
		if(tolower(static_cast<unsigned char>(str[start + i])) != suffix[i])
			return false;
	}
	return true;
}

bool IsSessionArchivePath(const string& path)
{
	return EndsWithNoCase(path, g_archiveExtension);
}

bool IsSessionConfigPath(const string& path)
{
	return EndsWithNoCase(path, g_configExtension);
}

bool IsSessionFilePath(const string& path)
{
	return EndsWithNoCase(path, g_sessionExtension);
}

static int SeekFile(FILE* fp, uint64_t offset)
{
	#ifdef _WIN32
		return _fseeki64(fp, offset, SEEK_SET);
	#else
		return fseeko(fp, offset, SEEK_SET);
	#endif
}

/**
	@brief File contents in a heap buffer
 */
class HeapSessionFileData : public SessionFileData
{
public:
	HeapSessionFileData(void* buf, size_t len, void (*freeFunc)(void*))
	: m_buf(buf)
	, m_freeFunc(freeFunc)
	{
		m_data = static_cast<const uint8_t*>(buf);
		m_size = len;
	}

	virtual ~HeapSessionFileData()
	{
		if(m_buf)
			m_freeFunc(m_buf);
	}

protected:
	void* m_buf;
	void (*m_freeFunc)(void*);
};

#ifndef _WIN32
/**
	@brief File contents memory mapped from disk
 */
class MappedSessionFileData : public SessionFileData
{
public:
	MappedSessionFileData(void* base, size_t mapLen, size_t delta, size_t len)
	: m_base(base)
	, m_mapLen(mapLen)
	{
		m_data = static_cast<const uint8_t*>(base) + delta;
		m_size = len;
	}

	virtual ~MappedSessionFileData()
	{
		munmap(m_base, m_mapLen);
	}

protected:
	void* m_base;
	size_t m_mapLen;
};
#endif

static void FreeMinizBuffer(void* p)
{
	mz_free(p);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SessionFileData

SessionFileData::~SessionFileData()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SessionWriter

SessionWriter::~SessionWriter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DirectorySessionWriter

DirectorySessionWriter::DirectorySessionWriter(const string& sessionPath, const string& dataDir)
	: m_sessionPath(sessionPath)
	, m_dataDir(dataDir)
{
}

bool DirectorySessionWriter::WriteFile(const string& relPath, const vector<Chunk>& chunks, bool /*compress*/)
{
	string path = m_dataDir + "/" + relPath;

	//Create any subdirectories it goes in
	error_code ec;
	filesystem::create_directories(filesystem::path(path).parent_path(), ec);
	if(ec)
	{
		m_error = string("Could not create the directory for \"") + path + "\": " + ec.message();
		return false;
	}

	FILE* fp = fopen(path.c_str(), "wb");
	if(!fp)
	{
		m_error = string("Could not open \"") + path + "\" for writing";
		return false;
	}

	for(auto& c : chunks)
	{
		if( (c.len > 0) && (fwrite(c.data, 1, c.len, fp) != c.len) )
		{
			fclose(fp);
			m_error = string("Failed to write \"") + path + "\"";
			return false;
		}
	}

	if(0 != fclose(fp))
	{
		m_error = string("Failed to write \"") + path + "\"";
		return false;
	}
	return true;
}

bool DirectorySessionWriter::WriteSessionFile(const string& yaml)
{
	FILE* fp = fopen(m_sessionPath.c_str(), "wb");
	if(!fp)
	{
		m_error = string("Could not open \"") + m_sessionPath + "\" for writing";
		return false;
	}

	bool ok = (fwrite(yaml.data(), 1, yaml.size(), fp) == yaml.size());
	if(0 != fclose(fp))
		ok = false;

	if(!ok)
		m_error = string("Failed to write \"") + m_sessionPath + "\"";
	return ok;
}

bool DirectorySessionWriter::Finish()
{
	//Files were written in place, so all that's left is to report any write that failed along the way
	return m_error.empty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZipSessionWriter

ZipSessionWriter::ZipSessionWriter(const string& archivePath)
	: m_archivePath(archivePath)
	, m_tempPath(archivePath + ".tmp")
	, m_baseName(filesystem::path(archivePath).stem().string())
	, m_zip(nullptr)
{
	auto zip = new mz_zip_archive;
	mz_zip_zero_struct(zip);
	if(!mz_zip_writer_init_file_v2(zip, m_tempPath.c_str(), 0, 0))
	{
		m_error = string("Could not create \"") + m_tempPath + "\": " +
			mz_zip_get_error_string(mz_zip_get_last_error(zip));
		delete zip;
		return;
	}
	m_zip = zip;
}

ZipSessionWriter::~ZipSessionWriter()
{
	//Not finished? Throw away the partial archive
	if(m_zip)
		Abort();
}

void ZipSessionWriter::Abort()
{
	auto zip = static_cast<mz_zip_archive*>(m_zip);
	mz_zip_writer_end(zip);
	delete zip;
	m_zip = nullptr;

	error_code ec;
	filesystem::remove(m_tempPath, ec);
}

///@brief Read callback for mz_zip_writer_add_read_buf_callback(), serving data from a list of chunks
struct ChunkReader
{
	const vector<SessionWriter::Chunk>* chunks;
};

static size_t ReadChunks(void* opaque, mz_uint64 fileOffset, void* buf, size_t n)
{
	auto& chunks = *static_cast<ChunkReader*>(opaque)->chunks;
	auto out = static_cast<uint8_t*>(buf);
	size_t copied = 0;

	uint64_t chunkStart = 0;
	for(auto& c : chunks)
	{
		uint64_t chunkEnd = chunkStart + c.len;
		uint64_t pos = fileOffset + copied;
		if( (pos >= chunkStart) && (pos < chunkEnd) && (copied < n) )
		{
			size_t count = min(static_cast<size_t>(chunkEnd - pos), n - copied);
			memcpy(out + copied, static_cast<const uint8_t*>(c.data) + (pos - chunkStart), count);
			copied += count;
		}
		chunkStart = chunkEnd;
	}

	return copied;
}

bool ZipSessionWriter::AddEntry(const string& name, const vector<Chunk>& chunks, bool compress)
{
	auto zip = static_cast<mz_zip_archive*>(m_zip);
	if(!zip)
		return false;

	//Raw sample data barely compresses and is slow to deflate, and stored data can be read in place when loading
	mz_uint level = compress ? MZ_DEFAULT_LEVEL : MZ_NO_COMPRESSION;

	mz_bool ok;
	if(chunks.size() == 1)
	{
		ok = mz_zip_writer_add_mem_ex(
			zip, name.c_str(), chunks[0].data, chunks[0].len, nullptr, 0, level, 0, 0);
	}
	else
	{
		uint64_t len = 0;
		for(auto& c : chunks)
			len += c.len;

		ChunkReader reader{&chunks};
		MZ_TIME_T now = time(nullptr);
		ok = mz_zip_writer_add_read_buf_callback(
			zip, name.c_str(), ReadChunks, &reader, len, &now, nullptr, 0, level, nullptr, 0, nullptr, 0);
	}

	if(!ok)
	{
		m_error = string("Failed to add \"") + name + "\" to \"" + m_tempPath + "\": " +
			mz_zip_get_error_string(mz_zip_get_last_error(zip));
		return false;
	}
	return true;
}

bool ZipSessionWriter::WriteFile(const string& relPath, const vector<Chunk>& chunks, bool compress)
{
	return AddEntry(m_baseName + "_data/" + relPath, chunks, compress);
}

bool ZipSessionWriter::WriteSessionFile(const string& yaml)
{
	return AddEntry(m_baseName + g_sessionExtension, { Chunk{yaml.data(), yaml.size()} }, true);
}

bool ZipSessionWriter::Finish()
{
	auto zip = static_cast<mz_zip_archive*>(m_zip);
	if(!zip)
		return false;

	//Don't replace the target if any file failed to be added
	if(!m_error.empty())
	{
		Abort();
		return false;
	}

	if(!mz_zip_writer_finalize_archive(zip))
	{
		m_error = string("Failed to write \"") + m_tempPath + "\": " +
			mz_zip_get_error_string(mz_zip_get_last_error(zip));
		Abort();
		return false;
	}
	bool ok = mz_zip_writer_end(zip);
	delete zip;
	m_zip = nullptr;
	if(!ok)
	{
		m_error = string("Failed to write \"") + m_tempPath + "\"";
		error_code ec;
		filesystem::remove(m_tempPath, ec);
		return false;
	}

	//Replace the target with the complete archive
	error_code ec;
	filesystem::rename(m_tempPath, m_archivePath, ec);
	if(ec)
	{
		m_error = string("Could not rename \"") + m_tempPath + "\" to \"" + m_archivePath + "\": " + ec.message();
		filesystem::remove(m_tempPath, ec);
		return false;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SessionReader

SessionReader::~SessionReader()
{
}

/**
	@brief Reads part of a file: memory maps it on POSIX, reads it into a buffer on Windows
 */
unique_ptr<SessionFileData> SessionReader::MapFile(const string& path, uint64_t offset, size_t len)
{
	if(len == 0)
		return make_unique<HeapSessionFileData>(nullptr, 0, free);

	#ifdef _WIN32
		FILE* fp = fopen(path.c_str(), "rb");
		if(!fp)
			return nullptr;
		if(0 != SeekFile(fp, offset))
		{
			fclose(fp);
			return nullptr;
		}

		//Read a megabyte at a time
		auto buf = static_cast<uint8_t*>(malloc(len));
		if(!buf)
		{
			fclose(fp);
			return nullptr;
		}
		size_t blocksize = 1024*1024;
		for(size_t pos = 0; pos < len; pos += blocksize)
		{
			size_t n = min(blocksize, len - pos);
			if(n != fread(buf + pos, 1, n, fp))
			{
				free(buf);
				fclose(fp);
				return nullptr;
			}
		}
		fclose(fp);
		return make_unique<HeapSessionFileData>(buf, len, free);

	#else
		int fd = open(path.c_str(), O_RDONLY);
		if(fd < 0)
			return nullptr;

		//Mapping has to start on a page boundary
		uint64_t page = sysconf(_SC_PAGESIZE);
		uint64_t start = offset & ~(page - 1);
		size_t delta = offset - start;
		void* base = mmap(nullptr, len + delta, PROT_READ, MAP_PRIVATE, fd, start);
		::close(fd);
		if(base == MAP_FAILED)
			return nullptr;
		return make_unique<MappedSessionFileData>(base, len + delta, delta, len);
	#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DirectorySessionReader

DirectorySessionReader::DirectorySessionReader(const string& sessionPath, const string& dataDir)
	: m_sessionPath(sessionPath)
	, m_dataDir(dataDir)
{
}

bool DirectorySessionReader::Exists(const string& relPath)
{
	error_code ec;
	return filesystem::exists(m_dataDir + "/" + relPath, ec);
}

unique_ptr<SessionFileData> DirectorySessionReader::ReadFile(const string& relPath)
{
	string path = m_dataDir + "/" + relPath;
	error_code ec;
	auto len = filesystem::file_size(path, ec);
	if(ec)
		return nullptr;
	return MapFile(path, 0, len);
}

bool DirectorySessionReader::ReadTextFile(const string& relPath, string& text)
{
	auto data = ReadFile(relPath);
	if(!data)
		return false;
	text.assign(reinterpret_cast<const char*>(data->data()), data->size());
	return true;
}

bool DirectorySessionReader::ReadSessionFile(string& yaml)
{
	error_code ec;
	auto len = filesystem::file_size(m_sessionPath, ec);
	auto data = ec ? nullptr : MapFile(m_sessionPath, 0, len);
	if(!data)
	{
		m_error = string("Could not read \"") + m_sessionPath + "\"";
		return false;
	}
	yaml.assign(reinterpret_cast<const char*>(data->data()), data->size());
	return true;
}

string DirectorySessionReader::Describe(const string& relPath)
{
	return m_dataDir + "/" + relPath;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZipSessionReader

ZipSessionReader::ZipSessionReader(const string& archivePath)
	: m_archivePath(archivePath)
	, m_zip(nullptr)
{
	auto zip = new mz_zip_archive;
	mz_zip_zero_struct(zip);
	if(!mz_zip_reader_init_file_v2(zip, archivePath.c_str(), 0, 0, 0))
	{
		m_error = string("Could not open \"") + archivePath + "\" as a zip file: " +
			mz_zip_get_error_string(mz_zip_get_last_error(zip));
		delete zip;
		return;
	}

	//Separate handle for reading local headers, so we know where the data of stored entries is
	FILE* fp = fopen(archivePath.c_str(), "rb");

	vector<string> sessionEntries;
	mz_uint nfiles = mz_zip_reader_get_num_files(zip);
	for(mz_uint i=0; i<nfiles; i++)
	{
		mz_zip_archive_file_stat stat;
		if(!mz_zip_reader_file_stat(zip, i, &stat) || stat.m_is_directory)
			continue;

		Entry e;
		e.index = i;
		e.size = stat.m_uncomp_size;
		e.stored = false;
		e.dataOffset = 0;

		//Stored entries: the data follows the local header (whose extra field may differ from the central directory's)
		if(fp && (stat.m_method == 0) && !stat.m_is_encrypted && (stat.m_comp_size == stat.m_uncomp_size))
		{
			uint8_t hdr[30];
			if( (0 == SeekFile(fp, stat.m_local_header_ofs)) && (sizeof(hdr) == fread(hdr, 1, sizeof(hdr), fp)) )
			{
				uint32_t sig = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (static_cast<uint32_t>(hdr[3]) << 24);
				uint32_t nameLen = hdr[26] | (hdr[27] << 8);
				uint32_t extraLen = hdr[28] | (hdr[29] << 8);
				if(sig == 0x04034b50)
				{
					e.stored = true;
					e.dataOffset = stat.m_local_header_ofs + sizeof(hdr) + nameLen + extraLen;
				}
			}
		}

		string name = stat.m_filename;
		if(EndsWith(name, g_sessionExtension))
			sessionEntries.push_back(name);
		m_entries[name] = e;
	}
	if(fp)
		fclose(fp);

	//There should be exactly one session in the archive. It's normally at the top level, but allow it to be in a
	//directory, in case the archive was made by zipping up a directory holding a session
	if(sessionEntries.size() != 1)
	{
		if(sessionEntries.empty())
			m_error = string("\"") + archivePath + "\" does not contain a .scopesession file";
		else
			m_error = string("\"") + archivePath + "\" contains more than one .scopesession file";
		mz_zip_reader_end(zip);
		delete zip;
		return;
	}
	m_sessionEntry = sessionEntries[0];
	m_dataPrefix = m_sessionEntry.substr(0, m_sessionEntry.length() - strlen(g_sessionExtension)) + "_data/";

	m_zip = zip;
}

ZipSessionReader::~ZipSessionReader()
{
	auto zip = static_cast<mz_zip_archive*>(m_zip);
	if(zip)
	{
		mz_zip_reader_end(zip);
		delete zip;
	}
}

unique_ptr<SessionFileData> ZipSessionReader::ReadEntry(const string& name)
{
	auto it = m_entries.find(name);
	if( (it == m_entries.end()) || !m_zip)
		return nullptr;
	auto& e = it->second;

	//Read stored data directly from the file
	if(e.stored)
		return MapFile(m_archivePath, e.dataOffset, e.size);

	//Decompress anything else
	lock_guard<mutex> lock(m_zipMutex);
	size_t len = 0;
	void* buf = mz_zip_reader_extract_to_heap(static_cast<mz_zip_archive*>(m_zip), e.index, &len, 0);
	if(!buf)
		return nullptr;
	return make_unique<HeapSessionFileData>(buf, len, FreeMinizBuffer);
}

bool ZipSessionReader::Exists(const string& relPath)
{
	return m_entries.find(m_dataPrefix + relPath) != m_entries.end();
}

unique_ptr<SessionFileData> ZipSessionReader::ReadFile(const string& relPath)
{
	return ReadEntry(m_dataPrefix + relPath);
}

bool ZipSessionReader::ReadTextFile(const string& relPath, string& text)
{
	auto data = ReadFile(relPath);
	if(!data)
		return false;
	text.assign(reinterpret_cast<const char*>(data->data()), data->size());
	return true;
}

bool ZipSessionReader::ReadSessionFile(string& yaml)
{
	auto data = ReadEntry(m_sessionEntry);
	if(!data)
	{
		m_error = string("Could not read \"") + m_sessionEntry + "\" from \"" + m_archivePath + "\"";
		return false;
	}
	yaml.assign(reinterpret_cast<const char*>(data->data()), data->size());
	return true;
}

string ZipSessionReader::Describe(const string& relPath)
{
	return m_archivePath + ":" + m_dataPrefix + relPath;
}
