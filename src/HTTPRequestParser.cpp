// -----------------------------------------------------------------
// libpion: a C++ framework for building lightweight HTTP interfaces
// -----------------------------------------------------------------
// Copyright (C) 2007 Atomic Labs, Inc.
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//

#include <libpion/HTTPRequestParser.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>


namespace pion {	// begin namespace pion

	
// static members of HTTPRequestParser

const unsigned int	HTTPRequestParser::METHOD_MAX = 1024;	// 1 KB
const unsigned int	HTTPRequestParser::RESOURCE_MAX = 256 * 1024;	// 256 KB
const unsigned int	HTTPRequestParser::QUERY_STRING_MAX = 1024 * 1024;	// 1 MB
const unsigned int	HTTPRequestParser::HEADER_NAME_MAX = 1024;	// 1 KB
const unsigned int	HTTPRequestParser::HEADER_VALUE_MAX = 1024 * 1024;	// 1 MB
const unsigned int	HTTPRequestParser::QUERY_NAME_MAX = 1024;	// 1 KB
const unsigned int	HTTPRequestParser::QUERY_VALUE_MAX = 1024 * 1024;	// 1 MB
const unsigned int	HTTPRequestParser::POST_CONTENT_MAX = 1024 * 1024;	// 1 MB
	
	
// HTTPRequestParser member functions

void HTTPRequestParser::readRequest(void)
{
	m_tcp_conn->getSocket().async_read_some(boost::asio::buffer(m_read_buffer),
											boost::bind(&HTTPRequestParser::readHeaderBytes,
														shared_from_this(),
														boost::asio::placeholders::error,
														boost::asio::placeholders::bytes_transferred));
}

void HTTPRequestParser::readHeaderBytes(const boost::asio::error& read_error,
										std::size_t bytes_read)
{
	if (read_error) {
		// a read error occured
		handleReadError(read_error);
		return;
	}

	PION_LOG_DEBUG(m_logger, "Read " << bytes_read << " bytes from HTTP request");
	
	// parse the bytes read from the last operation
	const char *read_ptr = m_read_buffer.data();
	const char * const read_end = read_ptr + bytes_read;
	boost::tribool result = parseRequestHeaders(read_ptr, bytes_read);

	PION_LOG_DEBUG(m_logger, "Parsed " << (read_ptr - m_read_buffer.data()) << " HTTP header bytes");

	if (result) {
		// finished reading request headers and they are valid

		// check if we have post content to read
		const unsigned long content_length =
			m_http_request->hasHeader(HTTPTypes::HEADER_CONTENT_LENGTH)
			? strtoul(m_http_request->getHeader(HTTPTypes::HEADER_CONTENT_LENGTH).c_str(), 0, 10)
			: 0;

		if (content_length == 0) {
			
			// there is no post content to read
			readContentBytes(read_error, 0);
			
		} else {

			// read the post content
			unsigned long content_bytes_to_read = content_length;
			m_http_request->setContentLength(content_length);
			char *post_buffer = m_http_request->createPostContentBuffer();
			
			if (read_ptr < read_end) {
				// there are extra bytes left from the last read operation
				// copy them into the beginning of the content buffer
				const unsigned int bytes_left_in_read_buffer = read_end - read_ptr;
				
				if (bytes_left_in_read_buffer >= content_length) {
					// the last read operation included all of the post content
					memcpy(post_buffer, read_ptr, content_length);
					content_bytes_to_read = 0;
					PION_LOG_DEBUG(m_logger, "Parsed " << content_length << " request content bytes from last read operation (finished)");
				} else {
					// only some of the post content has been read so far
					memcpy(post_buffer, read_ptr, bytes_left_in_read_buffer);
					content_bytes_to_read -= bytes_left_in_read_buffer;
					post_buffer += bytes_left_in_read_buffer;
					PION_LOG_DEBUG(m_logger, "Parsed " << bytes_left_in_read_buffer << " request content bytes from last read operation (partial)");
				}
			}

			if (content_bytes_to_read != 0) {
				// read the rest of the post content into the buffer
				// and only return after we've finished or an error occurs
				boost::asio::async_read(m_tcp_conn->getSocket(),
										boost::asio::buffer(post_buffer, content_bytes_to_read),
										boost::asio::transfer_at_least(content_bytes_to_read),
										boost::bind(&HTTPRequestParser::readContentBytes,
													shared_from_this(),
													boost::asio::placeholders::error,
													boost::asio::placeholders::bytes_transferred));
			}
		}
		
	} else if (!result) {
		// the request is invalid or an error occured

		m_http_request->setIsValid(false);
		m_request_handler(m_http_request, m_tcp_conn);
		
	} else {
		// not yet finished parsing the request -> read more data
		
		readRequest();
	}
}

void HTTPRequestParser::readContentBytes(const boost::asio::error& read_error,
										 std::size_t bytes_read)
{
	if (read_error) {
		// a read error occured
		handleReadError(read_error);
		return;
	}

	if (bytes_read != 0) {
		PION_LOG_DEBUG(m_logger, "Read " << bytes_read << " request content bytes (finished)");
	}

	// the request is valid
	m_http_request->setIsValid(true);
	
	// parse query pairs from the URI query string
	if (! m_http_request->getQueryString().empty()) {
		if (! parseURLEncoded(m_http_request->getQueryParams(),
							  m_http_request->getQueryString().c_str(),
							  m_http_request->getQueryString().size())) 
			PION_LOG_WARN(m_logger, "Request query string parsing failed (URI)");
	}
	
	// parse query pairs from post content (x-www-form-urlencoded)
	if (m_http_request->getHeader(HTTPTypes::HEADER_CONTENT_TYPE) ==
		HTTPTypes::CONTENT_TYPE_URLENCODED)
	{
		if (! parseURLEncoded(m_http_request->getQueryParams(),
							  m_http_request->getPostContent(),
							  m_http_request->getContentLength())) 
			PION_LOG_WARN(m_logger, "Request query string parsing failed (POST content)");
	}
	
	// call the request handler with the finished request
	m_request_handler(m_http_request, m_tcp_conn);
}

void HTTPRequestParser::handleReadError(const boost::asio::error& read_error)
{
	if (read_error == boost::asio::error::operation_aborted) {
		// if the operation was aborted, the acceptor was stopped,
		// which means another thread is shutting-down the server
		PION_LOG_INFO(m_logger, "HTTP request parsing aborted (shutting down)");
	} else {
		PION_LOG_INFO(m_logger, "HTTP request parsing aborted due to I/O error");
	}
	// close the connection, forcing the client to establish a new one
	m_tcp_conn->finish();
}

boost::tribool HTTPRequestParser::parseRequestHeaders(const char *& ptr,
													  const size_t len)
{
	// parse characters available in the read buffer
	const char * const end = ptr + len;
	
	while (ptr < end) {

		switch (m_parse_state) {
		case PARSE_METHOD_START:
			// we have not yet started parsing the HTTP method string
			if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr))
				return false;
			m_parse_state = PARSE_METHOD;
			m_method.erase();
			m_method.push_back(*ptr);
			break;

		case PARSE_METHOD:
			// we have started parsing the HTTP method string
			if (*ptr == ' ') {
				m_http_request->setMethod(m_method);
				m_resource.erase();
				m_parse_state = PARSE_URI_STEM;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else if (m_method.size() >= METHOD_MAX) {
				return false;
			} else {
				m_method.push_back(*ptr);
			}
			break;

		case PARSE_URI_STEM:
			// we have started parsing the URI stem (or resource name)
			if (*ptr == ' ') {
				m_http_request->setResource(m_resource);
				m_parse_state = PARSE_HTTP_VERSION_H;
			} else if (*ptr == '?') {
				m_http_request->setResource(m_resource);
				m_query_string.erase();
				m_parse_state = PARSE_URI_QUERY;
			} else if (isControl(*ptr)) {
				return false;
			} else if (m_resource.size() >= RESOURCE_MAX) {
				return false;
			} else {
				m_resource.push_back(*ptr);
			}
			break;

		case PARSE_URI_QUERY:
			// we have started parsing the URI query string
			if (*ptr == ' ') {
				m_http_request->setQueryString(m_query_string);
				m_parse_state = PARSE_HTTP_VERSION_H;
			} else if (isControl(*ptr)) {
				return false;
			} else if (m_query_string.size() >= QUERY_STRING_MAX) {
				return false;
			} else {
				m_query_string.push_back(*ptr);
			}
			break;
			
		case PARSE_HTTP_VERSION_H:
			// parsing "HTTP"
			if (*ptr != 'H') return false;
			m_parse_state = PARSE_HTTP_VERSION_T_1;
			break;

		case PARSE_HTTP_VERSION_T_1:
			// parsing "HTTP"
			if (*ptr != 'T') return false;
			m_parse_state = PARSE_HTTP_VERSION_T_2;
			break;

		case PARSE_HTTP_VERSION_T_2:
			// parsing "HTTP"
			if (*ptr != 'T') return false;
			m_parse_state = PARSE_HTTP_VERSION_P;
			break;

		case PARSE_HTTP_VERSION_P:
			// parsing "HTTP"
			if (*ptr != 'P') return false;
			m_parse_state = PARSE_HTTP_VERSION_SLASH;
			break;

		case PARSE_HTTP_VERSION_SLASH:
			// parsing slash after "HTTP"
			if (*ptr != '/') return false;
			m_parse_state = PARSE_HTTP_VERSION_MAJOR_START;
			break;

		case PARSE_HTTP_VERSION_MAJOR_START:
			// parsing the first digit of the major version number
			if (!isDigit(*ptr)) return false;
			m_http_request->setVersionMajor(*ptr - '0');
			m_parse_state = PARSE_HTTP_VERSION_MAJOR;
			break;

		case PARSE_HTTP_VERSION_MAJOR:
			// parsing the major version number (not first digit)
			if (*ptr == '.') {
				m_parse_state = PARSE_HTTP_VERSION_MINOR_START;
			} else if (isDigit(*ptr)) {
				m_http_request->setVersionMajor( (m_http_request->getVersionMajor() * 10)
												 + (*ptr - '0') );
			} else {
				return false;
			}
			break;

		case PARSE_HTTP_VERSION_MINOR_START:
			// parsing the first digit of the minor version number
			if (!isDigit(*ptr)) return false;
			m_http_request->setVersionMinor(*ptr - '0');
			m_parse_state = PARSE_HTTP_VERSION_MINOR;
			break;

		case PARSE_HTTP_VERSION_MINOR:
			// parsing the major version number (not first digit)
			if (*ptr == '\r') {
				m_parse_state = PARSE_EXPECTING_NEWLINE;
			} else if (*ptr == '\n') {
				m_parse_state = PARSE_EXPECTING_CR;
			} else if (isDigit(*ptr)) {
				m_http_request->setVersionMinor( (m_http_request->getVersionMinor() * 10)
												 + (*ptr - '0') );
			} else {
				return false;
			}
			break;

		case PARSE_EXPECTING_NEWLINE:
			// we received a CR; expecting a newline to follow
			if (*ptr == '\n') {
				m_parse_state = PARSE_HEADER_START;
			} else if (*ptr == '\r') {
				// we received two CR's in a row
				// assume CR only is (incorrectly) being used for line termination
				// therefore, the request is finished
				++ptr;
				return true;
			} else if (*ptr == '\t' || *ptr == ' ') {
				m_parse_state = PARSE_HEADER_WHITESPACE;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else {
				// assume it is the first character for the name of a header
				m_header_name.erase();
				m_header_name.push_back(*ptr);
				m_parse_state = PARSE_HEADER_NAME;
			}
			break;

		case PARSE_EXPECTING_CR:
			// we received a newline without a CR
			if (*ptr == '\r') {
				m_parse_state = PARSE_HEADER_START;
			} else if (*ptr == '\n') {
				// we received two newlines in a row
				// assume newline only is (incorrectly) being used for line termination
				// therefore, the request is finished
				++ptr;
				return true;
			} else if (*ptr == '\t' || *ptr == ' ') {
				m_parse_state = PARSE_HEADER_WHITESPACE;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else {
				// assume it is the first character for the name of a header
				m_header_name.erase();
				m_header_name.push_back(*ptr);
				m_parse_state = PARSE_HEADER_NAME;
			}
			break;

		case PARSE_HEADER_WHITESPACE:
			// parsing whitespace before a header name
			if (*ptr == '\r') {
				m_parse_state = PARSE_EXPECTING_NEWLINE;
			} else if (*ptr == '\n') {
				m_parse_state = PARSE_EXPECTING_CR;
			} else if (*ptr != '\t' && *ptr != ' ') {
				if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr))
					return false;
				// assume it is the first character for the name of a header
				m_header_name.erase();
				m_header_name.push_back(*ptr);
				m_parse_state = PARSE_HEADER_NAME;
			}
			break;

		case PARSE_HEADER_START:
			// parsing the start of a new header
			if (*ptr == '\r') {
				m_parse_state = PARSE_EXPECTING_FINAL_NEWLINE;
			} else if (*ptr == '\n') {
				m_parse_state = PARSE_EXPECTING_FINAL_CR;
			} else if (*ptr == '\t' || *ptr == ' ') {
				m_parse_state = PARSE_HEADER_WHITESPACE;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else {
				// first character for the name of a header
				m_header_name.erase();
				m_header_name.push_back(*ptr);
				m_parse_state = PARSE_HEADER_NAME;
			}
			break;

		case PARSE_HEADER_NAME:
			// parsing the name of a header
			if (*ptr == ':') {
				m_header_value.erase();
				m_parse_state = PARSE_SPACE_BEFORE_HEADER_VALUE;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else if (m_header_name.size() >= HEADER_NAME_MAX) {
				return false;
			} else {
				// character (not first) for the name of a header
				m_header_name.push_back(*ptr);
			}
			break;

		case PARSE_SPACE_BEFORE_HEADER_VALUE:
			// parsing space character before a header's value
			if (*ptr == ' ') {
				m_parse_state = PARSE_HEADER_VALUE;
			} else if (*ptr == '\r') {
				m_http_request->addHeader(m_header_name, m_header_value);
				m_parse_state = PARSE_EXPECTING_NEWLINE;
			} else if (*ptr == '\n') {
				m_http_request->addHeader(m_header_name, m_header_value);
				m_parse_state = PARSE_EXPECTING_CR;
			} else if (!isChar(*ptr) || isControl(*ptr) || isSpecial(*ptr)) {
				return false;
			} else {
				// assume it is the first character for the value of a header
				m_header_value.push_back(*ptr);
				m_parse_state = PARSE_HEADER_VALUE;
			}
			break;

		case PARSE_HEADER_VALUE:
			// parsing the value of a header
			if (*ptr == '\r') {
				m_http_request->addHeader(m_header_name, m_header_value);
				m_parse_state = PARSE_EXPECTING_NEWLINE;
			} else if (*ptr == '\n') {
				m_http_request->addHeader(m_header_name, m_header_value);
				m_parse_state = PARSE_EXPECTING_CR;
			} else if (isControl(*ptr)) {
				return false;
			} else if (m_header_value.size() >= HEADER_VALUE_MAX) {
				return false;
			} else {
				// character (not first) for the value of a header
				m_header_value.push_back(*ptr);
			}
			break;

		case PARSE_EXPECTING_FINAL_NEWLINE:
			if (*ptr == '\n') ++ptr;
			return true;

		case PARSE_EXPECTING_FINAL_CR:
			if (*ptr == '\r') ++ptr;
			return true;
		}

		++ptr;
	}

	return boost::indeterminate;
}

bool HTTPRequestParser::parseURLEncoded(HTTPTypes::StringDictionary& dict,
										const char *ptr, const size_t len)
{
	// used to track whether we are parsing the name or value
	enum QueryParseState {
		QUERY_PARSE_NAME, QUERY_PARSE_VALUE
	};
	QueryParseState parse_state = QUERY_PARSE_NAME;

	// misc other variables used for parsing
	const char * const end = ptr + len;
	std::string query_name;
	std::string query_value;
	
	// iterate through each encoded character
	while (ptr < end) {
		switch (parse_state) {
		
		case QUERY_PARSE_NAME:
			// parsing query name
			if (*ptr == '=') {
				// end of name found
				if (query_name.empty()) return false;
				parse_state = QUERY_PARSE_VALUE;
			} else if (*ptr == '&') {
				// value is empty (OK)
				if (query_name.empty()) return false;
				dict.insert( std::make_pair(query_name, query_value) );
				query_name.erase();
			} else if (isControl(*ptr) || query_name.size() >= QUERY_NAME_MAX) {
				// control character detected, or max sized exceeded
				return false;
			} else {
				// character is part of the name
				query_name.push_back(*ptr);
			}
			break;

		case QUERY_PARSE_VALUE:
			// parsing query value
			if (*ptr == '&') {
				// end of value found (OK if empty)
				dict.insert( std::make_pair(query_name, query_value) );
				query_name.erase();
				query_value.erase();
				parse_state = QUERY_PARSE_NAME;
			} else if (isControl(*ptr) || query_value.size() >= QUERY_VALUE_MAX) {
				// control character detected, or max sized exceeded
				return false;
			} else {
				// character is part of the value
				query_value.push_back(*ptr);
			}
			break;
		}
		
		++ptr;
	}
	
	// handle last pair in string
	if (! query_name.empty())
		dict.insert( std::make_pair(query_name, query_value) );
	
	return true;
}

bool HTTPRequestParser::parseCookieEncoded(HTTPTypes::StringDictionary& dict,
										   const std::string& encoded_string)
{
	/// not yet implemented
	return false;
}

bool HTTPRequestParser::parseMultipartEncoded(HTTPTypes::StringDictionary& dict,
											  TCPConnectionPtr& conn)
{
	/// not yet implemented
	return false;
}


}	// end namespace pion
