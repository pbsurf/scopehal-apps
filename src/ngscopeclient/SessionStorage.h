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
	@brief Declaration of SessionWriter, SessionReader and their implementations
 */
#ifndef SessionStorage_h
#define SessionStorage_h

#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <vector>

/**
	@brief Contents of a file read from a saved session

	The data is either memory mapped or copied into a buffer, and is released when this object is destroyed.
 */
class SessionFileData
{
public:
	SessionFileData(const SessionFileData&) =delete;
	SessionFileData& operator=(const SessionFileData&) =delete;
	virtual ~SessionFileData();

	const uint8_t* data() const
	{ return m_data; }

	size_t size() const
	{ return m_size; }

protected:
	SessionFileData()
	: m_data(nullptr)
	, m_size(0)
	{}

	const uint8_t* m_data;
	size_t m_size;

};

/**
	@brief Writes the files making up a saved session

	A session is a .scopesession YAML file plus a data directory holding everything else (window layout, lab notes,
	waveforms...). Files in the data directory are named by paths relative to it, with '/' separators.
 */
class SessionWriter
{
public:
	virtual ~SessionWriter();

	///@brief A piece of a file being written
	struct Chunk
	{
		const void* data;
		size_t len;
	};

	/**
		@brief Writes a file in the data directory from one or more pieces, concatenated

		@param relPath		Path relative to the data directory
		@param chunks		Contents of the file
		@param compress		Hint that the data is worth compressing (text), as opposed to raw sample data
	 */
	virtual bool WriteFile(const std::string& relPath, const std::vector<Chunk>& chunks, bool compress) =0;

	///@brief Writes binary data to a file in the data directory
	bool WriteFile(const std::string& relPath, const void* data, size_t len)
	{ return WriteFile(relPath, { Chunk{data, len} }, false); }

	///@brief Writes text to a file in the data directory
	bool WriteTextFile(const std::string& relPath, const std::string& text)
	{ return WriteFile(relPath, { Chunk{text.data(), text.size()} }, true); }

	///@brief Writes the .scopesession YAML
	virtual bool WriteSessionFile(const std::string& yaml) =0;

	/**
		@brief Completes the save once all files have been written

		Fails if writing any of the files failed. Until this succeeds, the session may not have been written (or
		replaced) on disk
	 */
	virtual bool Finish() =0;

	///@brief Describes why the last operation failed
	const std::string& GetError()
	{ return m_error; }

protected:
	std::string m_error;
};

/**
	@brief Saves a session as a .scopesession file plus a _data directory next to it
 */
class DirectorySessionWriter : public SessionWriter
{
public:
	DirectorySessionWriter(const std::string& sessionPath, const std::string& dataDir);

	virtual bool WriteFile(const std::string& relPath, const std::vector<Chunk>& chunks, bool compress) override;
	virtual bool WriteSessionFile(const std::string& yaml) override;
	virtual bool Finish() override;

protected:
	std::string m_sessionPath;
	std::string m_dataDir;
};

/**
	@brief Saves a session as a single .scopearchive (or .scopeconfig) zip file

	The archive contains the same files as a directory session: NAME.scopesession and NAME_data/..., so it can be
	unzipped and opened as a normal session. Waveform data is stored uncompressed so it can be read in place when
	loading, everything else is compressed.

	The archive is written to a temporary file which replaces the target in Finish(), so a failed save doesn't destroy
	an existing archive.
 */
class ZipSessionWriter : public SessionWriter
{
public:
	ZipSessionWriter(const std::string& archivePath);
	virtual ~ZipSessionWriter();

	bool IsOpen()
	{ return m_zip != nullptr; }

	virtual bool WriteFile(const std::string& relPath, const std::vector<Chunk>& chunks, bool compress) override;
	virtual bool WriteSessionFile(const std::string& yaml) override;
	virtual bool Finish() override;

protected:
	bool AddEntry(const std::string& name, const std::vector<Chunk>& chunks, bool compress);
	void Abort();

	std::string m_archivePath;
	std::string m_tempPath;

	///@brief Name of the session (archive file name without extension), used for the paths inside the archive
	std::string m_baseName;

	///@brief The mz_zip_archive (opaque here so miniz.h stays out of this header)
	void* m_zip;
};

/**
	@brief Reads the files making up a saved session
 */
class SessionReader
{
public:
	virtual ~SessionReader();

	///@brief Checks if a file exists in the data directory
	virtual bool Exists(const std::string& relPath) =0;

	///@brief Reads a text file from the data directory. Returns false if it doesn't exist or can't be read
	virtual bool ReadTextFile(const std::string& relPath, std::string& text) =0;

	/**
		@brief Reads a binary file from the data directory. Returns nullptr if it doesn't exist or can't be read

		Safe to call from multiple threads at once
	 */
	virtual std::unique_ptr<SessionFileData> ReadFile(const std::string& relPath) =0;

	///@brief Reads the .scopesession YAML
	virtual bool ReadSessionFile(std::string& yaml) =0;

	///@brief Describes a file in the data directory for log and error messages
	virtual std::string Describe(const std::string& relPath) =0;

	///@brief Describes why the last operation failed
	const std::string& GetError()
	{ return m_error; }

protected:
	static std::unique_ptr<SessionFileData> MapFile(const std::string& path, uint64_t offset, size_t len);

	std::string m_error;
};

/**
	@brief Reads a session saved as a .scopesession file plus a _data directory
 */
class DirectorySessionReader : public SessionReader
{
public:
	DirectorySessionReader(const std::string& sessionPath, const std::string& dataDir);

	virtual bool Exists(const std::string& relPath) override;
	virtual bool ReadTextFile(const std::string& relPath, std::string& text) override;
	virtual std::unique_ptr<SessionFileData> ReadFile(const std::string& relPath) override;
	virtual bool ReadSessionFile(std::string& yaml) override;
	virtual std::string Describe(const std::string& relPath) override;

protected:
	std::string m_sessionPath;
	std::string m_dataDir;
};

/**
	@brief Reads a session saved as a .scopearchive (or .scopeconfig) zip file

	Uncompressed entries are read straight from the archive file (memory mapped on POSIX), without going through miniz.
 */
class ZipSessionReader : public SessionReader
{
public:
	ZipSessionReader(const std::string& archivePath);
	virtual ~ZipSessionReader();

	///@brief Checks if the archive was opened and contains a session
	bool IsOpen()
	{ return m_zip != nullptr; }

	virtual bool Exists(const std::string& relPath) override;
	virtual bool ReadTextFile(const std::string& relPath, std::string& text) override;
	virtual std::unique_ptr<SessionFileData> ReadFile(const std::string& relPath) override;
	virtual bool ReadSessionFile(std::string& yaml) override;
	virtual std::string Describe(const std::string& relPath) override;

protected:
	std::unique_ptr<SessionFileData> ReadEntry(const std::string& name);

	///@brief Location of an entry in the archive
	struct Entry
	{
		unsigned int index;

		///@brief True if stored uncompressed, so the data can be read directly from the archive file
		bool stored;

		///@brief Offset of the data in the archive file (only valid if stored)
		uint64_t dataOffset;

		uint64_t size;
	};

	std::string m_archivePath;

	///@brief Name of the .scopesession entry
	std::string m_sessionEntry;

	///@brief Prefix of data directory entries (NAME_data/)
	std::string m_dataPrefix;

	///@brief Entries by name
	std::map<std::string, Entry> m_entries;

	///@brief The mz_zip_archive (opaque here so miniz.h stays out of this header)
	void* m_zip;

	///@brief miniz isn't thread safe, so serialize access to m_zip
	std::mutex m_zipMutex;
};

///@brief Checks if a path names a session archive (.scopearchive) rather than a .scopesession file
bool IsSessionArchivePath(const std::string& path);

///@brief Checks if a path names a session configuration (.scopeconfig): an archive without waveform data
bool IsSessionConfigPath(const std::string& path);

///@brief Checks if a path names a .scopesession file
bool IsSessionFilePath(const std::string& path);

#endif
