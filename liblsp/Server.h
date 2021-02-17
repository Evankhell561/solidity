/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
#pragma once

#include <liblsp/Transport.h>
#include <liblsp/Range.h>

#include <libsolutil/URI.h>

#include <json/value.h>

#include <functional>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <variant>

namespace lsp {

class Transport;

enum class ErrorCode;

// {{{ Helper types
enum class Trace { Off, Messages, Verbose };

struct WorkspaceFolder {
	std::string name; // The name of the workspace folder. Used to refer to this workspace folder in the user interface.
	solidity::util::URI uri;  // The associated URI for this workspace folder.
};

struct DocumentPosition {
	solidity::util::URI uri;
	Position position;
};

struct ServerId {
	std::string serverName;
	std::string serverVersion;
};

enum class DocumentHighlightKind {
	Unspecified,
	Text,           //!< a textual occurrence
	Read,           //!< read access to a variable
	Write,          //!< write access to a variable
};

struct Location {
	solidity::util::URI uri;
	Range range;
};

struct DocumentHighlight {
	Range range;
	DocumentHighlightKind kind = DocumentHighlightKind::Unspecified;
};

enum class DiagnosticSeverity {
	Error = 1,
	Warning = 2,
	Information = 3,
	Hint = 4,
};

enum class DiagnosticTag {
	Unnecessary = 1, // Unused or unnecessary code.
	Deprecated = 2   // Deprecated or obsolete code.
};

/// Represents a related message and source code location for a diagnostic. This should be
/// used to point to code locations that cause or related to a diagnostics, e.g when duplicating
/// a symbol in a scope.
struct DiagnosticRelatedInformation {
	Location location;   // The location of this related diagnostic information.
	std::string message; // The message of this related diagnostic information.
};

/// Represents a diagnostic, such as a compiler error or warning. Diagnostic objects are only valid in the scope of a resource.
struct Diagnostic {
	Range range;                                   // The range at which the message applies.
	std::optional<DiagnosticSeverity> severity;
	std::optional<unsigned long long> code;        // The diagnostic's code, which might appear in the user interface.
	std::optional<std::string> source;             // A human-readable string describing the source of this diagnostic, e.g. 'typescript' or 'super lint'.
	std::string message;                           // The diagnostic's message.
	std::vector<DiagnosticTag> diagnosticTag;      // Additional metadata about the diagnostic.
	std::vector<DiagnosticRelatedInformation> relatedInformation; // An array of related diagnostic information, e.g. when symbol-names within a scope collide all definitions can be marked via this property.
};
// }}}

/// Solidity Language Server, managing one LSP client.
///
/// This implements a subset of LSP version 3.16 that can be found at:
///     https://microsoft.github.io/language-server-protocol/specifications/specification-3-16/
class Server
{
private:
	Server(Server const&) = delete;
	Server& operator=(Server const&) = delete;

public:
	/// Constructs a Language Server that is communicating over stdio via JSON-RPC.
	///
	/// @param _client the transport layer to the connected client
	/// @param _logger a logging stream, used internally for logging debug/warning/error messages.
	explicit Server(Transport& _client, std::function<void(std::string_view)> _logger);

	virtual ~Server() = default;

	/// Loops over incoming messages via the transport layer until shutdown condition is met.
	///
	/// The standard shutdown condition is when the maximum number of consecutive failures
	/// has been exceeded.
	///
	/// @return boolean indicating normal or abnormal termination.
	bool run();

	/// Handles a JSON-RPC message in string form.
	void handleMessage(std::string const& _message);

	/// Handles a JSON-RPC message.
	void handleMessage(Json::Value const& _jsonMessage);

	// {{{ Client-to-Server API
	/// Invoked by the client to trigger server initialization.
	virtual ServerId initialize(solidity::util::URI _rootUri, std::vector<WorkspaceFolder> _workspaceFolders) = 0;

	/// Notification being sent when the client has finished initialization.
	virtual void initialized() {}

	/// Invoked when the server user-supplied configuration changes (initiated by the client).
	virtual void changeConfiguration(Json::Value const&) {}

	/// The given document was opened.
	///
	/// @param _uri
	/// @param _languageId
	/// @param _version
	/// @param _contents
	virtual void documentOpened(solidity::util::URI const& /*_uri*/, std::string /*_languageId*/, int /*_version*/, std::string /*_contents*/) {}

	/// The given document was fully replaced with @p _contents.
	///
	/// @param _uri
	/// @param _documentVersion
	/// @param _contents
	virtual void documentContentUpdated(solidity::util::URI const& /*_uri*/, std::optional<int> /*_version*/, std::string const& /*_fullContentChange*/) {}

	/// Invoked to notify LSP implementation that updates have happened to the given document.
	virtual void documentContentUpdated(solidity::util::URI const& /*_uri*/) {}

	/// The given document was partially updated at @p _range with @p _contents.
	///
	/// @param _uri
	/// @param _version
	/// @param _range The content range to replace
	/// @param _range The range that is going to be replaced
	/// @param _text the replacement text
	virtual void documentContentUpdated(
		solidity::util::URI const& /*_uri*/,
		std::optional<int> /*_version*/,
		Range /*_range*/,
		std::string const& /*_text*/
	) {}

	/// The given document was closed.
	virtual void documentClosed(solidity::util::URI const& /*_uri*/) {}

	/// IDE action: "Go to definition"
	///
	/// @param _position the cursor position in the current document.
	///
	/// @returns a list of ranges that define the symbol under the current location.
	/// @returns specific range of the definition of one or more referencing symbol.
	virtual std::vector<Location> gotoDefinition(DocumentPosition /*_position*/) { return {}; }

	/// Find all semantically equivalent occurrences of the symbol the current cursor is located at.
	///
	/// @returns a list of ranges to highlight as well as their use kind (read fraom, written to, other text).
	virtual std::vector<DocumentHighlight> semanticHighlight(DocumentPosition /*_documentPosition*/) { return {}; }

	/// Finds all references of the current symbol at the given document position.
	///
	/// @returns all references as document ranges as well as their use kind (read fraom, written to, other text).
	virtual std::vector<Location> references(DocumentPosition /*_documentPosition*/) { return {}; }
	// }}}

	/// Sends a message to the client updating diagnostics for given URI at given document version.
	///
	/// @param _uri         The URI for which diagnostic information is reported.
	/// @param _version     Optional the version number of the document the diagnostics are published for.
	/// @param _diagnostics An array of diagnostic information items.
	void pushDiagnostics(
		solidity::util::URI const& _uri,
		std::optional<int> _version,
		std::vector<Diagnostic> const& _diagnostics
	);

	/// Sends a message to the client.
	///
	/// @param _id an optional request ID that this response relates to
	/// @param _message the message to send to the client
	void error(MessageId const& _id, ErrorCode, std::string const& _message);

	/// Logs a message (should be used for logging messages that are informationally useful to the client).
	void log(std::string const& _message);

	/// Logs a verbose trace message (should used for logging messages that are helpful to the client).
	void trace(std::string const& _message);

	/// Retrieves the trace-level as configured by the client.
	Trace trace() const noexcept { return m_trace; }

protected:
	void handle_initializeRequest(MessageId _id, Json::Value const& _args);
	void handle_exit(MessageId _id, Json::Value const& _args);
	void handle_workspace_didChangeConfiguration(MessageId _id, Json::Value const& _args);
	void handle_textDocument_didOpen(MessageId _id, Json::Value const& _args);
	void handle_textDocument_didChange(MessageId _id, Json::Value const& _args);
	void handle_textDocument_didClose(MessageId _id, Json::Value const& _args);
	void handle_textDocument_definition(MessageId _id, Json::Value const& _args);
	void handle_textDocument_highlight(MessageId _id, Json::Value const& _args);
	void handle_textDocument_references(MessageId _id, Json::Value const& _args);

private:
	using Handler = std::function<void(MessageId, Json::Value const&)>;
	using HandlerMap = std::unordered_map<std::string, Handler>;

	Transport& m_client;
	HandlerMap m_handlers;
	bool m_shutdownRequested = false;
	bool m_exitRequested = false;
	::lsp::Trace m_trace = ::lsp::Trace::Off;
	std::function<void(std::string_view)> m_logger;
};

} // namespace solidity
