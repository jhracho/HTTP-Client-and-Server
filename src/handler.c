/* handler.c: HTTP Request Handlers */

#include "spidey.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* Internal Declarations */
Status handle_browse_request(Request *request);
Status handle_file_request(Request *request);
Status handle_cgi_request(Request *request);
Status handle_error(Request *request, Status status);

/**
 * Handle HTTP Request.
 *
 * @param   r           HTTP Request structure
 * @return  Status of the HTTP request.
 *
 * This parses a request, determines the request path, determines the request
 * type, and then dispatches to the appropriate handler type.
 *
 * On error, handle_error should be used with an appropriate HTTP status code.
 **/
Status  handle_request(Request *r) {
    Status result;

    /* Parse request: parse_request_method */
    int requestSuccess = parse_request(r);
    if (requestSuccess == -1 || !r->method || !r->uri) {
        debug("Bad request 1");
        return handle_error(r, HTTP_STATUS_BAD_REQUEST);
    }

    /* Determine request path */
    r->path = determine_request_path(r->uri);
    if (!r->path) {
        debug("Bad request 2");
        handle_error(r, HTTP_STATUS_BAD_REQUEST);
    }
    
    debug("HTTP REQUEST PATH: %s", r->path);

    /* Dispatch to appropriate request handler type based on file type */
    struct stat s;
    if (stat(r->path, &s) == 0){
        if (S_ISDIR(s.st_mode))
            result = handle_browse_request(r);

        else if(S_ISREG(s.st_mode) && access(r->path, R_OK) == 0){
            if (access(r->path, X_OK) == 0)
                result = handle_cgi_request(r);
            else
                result = handle_file_request(r);
        }
    }

    // Checking for stat failure
    else{
        result = HTTP_STATUS_BAD_REQUEST;
        return handle_error(r, result);
    }

    // If something goes wrong
    if (result != HTTP_STATUS_OK)
        return handle_error(r, result);


    log("HTTP REQUEST STATUS: %s", http_status_string(result));

    // Freeing everything
    return result;
}

/**
 * Handle browse request.
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP browse request.
 *
 * This lists the contents of a directory in HTML.
 *
 * If the path cannot be opened or scanned as a directory, then handle error
 * with HTTP_STATUS_NOT_FOUND.
 **/
Status  handle_browse_request(Request *r) {
    debug("Handling Directory Request");
    struct dirent **entries;
    int n;

    /* Open a directory for reading or scanning */
    n = scandir(r->path, &entries, 0, alphasort);
    if(n < 0) {
        debug("Scandir Failed: %s", strerror(errno));
        return handle_error(r, HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }

    /* Write HTTP Header with OK Status and text/html Content-Type */
    fprintf(r->stream, "HTTP/1.0 200 OK\r\n");
    fprintf(r->stream, "Content-Type: text/html\r\n");
    fprintf(r->stream, "\r\n");

    /* For each entry in directory, emit HTML list item */
    fprintf(r->stream, "<ul>\n");
    for(int i = 0; i < n; i++) {
        if (streq(entries[i]->d_name, "."))
            continue;
        
        fprintf(r->stream, "<li>");
        if(streq(r->uri, "/")) {
            fprintf(r->stream, "<a href=\"%s\">", entries[i]->d_name);
        }
        else {
            fprintf(r->stream, "<a href=\"%s/%s\">", r->uri, entries[i]->d_name);
        }
        //fprintf(r->stream, "<a href=\"%s/%s\">", r->uri, entries[i]->d_name);
        fprintf(r->stream, "%s", entries[i]->d_name);
        fprintf(r->stream, "</a>");
        fprintf(r->stream, "</li>\n");
        free(entries[i]);
    }
    fprintf(r->stream, "</ul>\n");

    free(entries);

    /* Return OK */
    return HTTP_STATUS_OK;
}

/**
 * Handle file request.
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP file request.
 *
 * This opens and streams the contents of the specified file to the socket.
 *
 * If the path cannot be opened for reading, then handle error with
 * HTTP_STATUS_NOT_FOUND.
 **/
Status  handle_file_request(Request *r) {
    debug("Handling File Request");
    FILE *fs = NULL;
    char buffer[BUFSIZ];
    char *mimetype = NULL;
    size_t nread = 0;

    /* Open file for reading */
    fs = fopen(r->path, "r");
    if(!fs) {
        fclose(fs);
        //free(mimetype);
        return handle_error(r, HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }
    
    /* Determine mimetype */
    mimetype = determine_mimetype(r->path);
    //debug("Mimetype: %s", mimetype);
    /* Write HTTP Headers with OK status and determined Content-Type */
    fprintf(r->stream, "HTTP/1.0 200 OK\r\n");
    fprintf(r->stream, "Content-Type: %s\r\n", mimetype);
   // fprintf(r->stream, "Content-Type: text/html\r\n");
    fprintf(r->stream, "\r\n");


    /* Read from file and write to socket in chunks */
    nread = fread(buffer, 1, BUFSIZ, fs);
    while(nread > 0) {
        fwrite(buffer, 1, nread, r->stream);
        nread = fread(buffer, 1, BUFSIZ, fs);
    }

    /* Close file, deallocate mimetype, return OK */
    fclose(fs);

    return HTTP_STATUS_OK;
}

/**
 * Handle CGI request
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP file request.
 *
 * This popens and streams the results of the specified executables to the
 * socket.
 *
 * If the path cannot be popened, then handle error with
 * HTTP_STATUS_INTERNAL_SERVER_ERROR.
 **/
Status  handle_cgi_request(Request *r) {
    FILE *pfs;
    char buffer[BUFSIZ];

    /* Export CGI environment variables from request:
     * http://en.wikipedia.org/wiki/Common_Gateway_Interface */
    setenv("DOCUMENT_ROOT", RootPath, 1);
    setenv("QUERY_STRING",r->query, 1);
    setenv("REMOTE_ADDR", r->host, 1);
    setenv("REMOTE_PORT", r->port, 1);
    setenv("REQUEST_METHOD", r->method, 1);
    setenv("REQUEST_URI", r->uri, 1);
    setenv("SCRIPT_FILENAME", r->path, 1);
    setenv("SERVER_PORT", Port, 1);

    /* Export CGI environment variables from request headers */
    // host, accept, accept-language, accept-encoding, connection, user-agent
    Header *temp = r->headers;
    if (!temp)
        return HTTP_STATUS_BAD_REQUEST;

    while(temp) {
        if(streq(temp->name,      "Host")) {
            setenv("HTTP_HOST",            temp->data, 1);
        }
        else if(streq(temp->name, "Accept")) {
            setenv("HTTP_ACCEPT",          temp->data, 1);
        }
        else if(streq(temp->name, "Accept-Language")) {
            setenv("HTTP_ACCEPT_LANGUAGE", temp->data, 1);
        }
        else if(streq(temp->name, "Accept-Encoding")) {
            setenv("HTTP_ACCEPT_ENCODING", temp->data, 1);
        }
        else if(streq(temp->name, "Connection")) {
            setenv("HTTP_CONNECTION",      temp->data, 1);
        }
        else if(streq(temp->name, "User-Agent")) {
            setenv("HTTP_USER_AGENT",      temp->data, 1);
        }

        temp = temp->next;
    }

    /* POpen CGI Script */
    if(!r->path) {
        return handle_error(r, HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }
    pfs = popen(r->path, "r");
    if(pfs < 0) {
        pclose(pfs);
        return handle_error(r, HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }

    /* Copy data from popen to socket */
    size_t nread = fread(buffer, 1, BUFSIZ, pfs);
    while(nread > 0) {
        fwrite(buffer, 1, nread, r->stream);
        nread = fread(buffer , 1, BUFSIZ, pfs);
    }

    /* Close popen, return OK */
    pclose(pfs);
    return HTTP_STATUS_OK;
}

/**
 * Handle displaying error page
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP error request.
 *
 * This writes an HTTP status error code and then generates an HTML message to
 * notify the user of the error.
 **/
Status  handle_error(Request *r, Status status) {
    const char *status_string = http_status_string(status);
    debug("Reached handle_error");
    debug("%p", r->stream);

    /* Write HTTP Header */
    fprintf(r->stream, "HTTP/1.0 %s\r\n", status_string);
    fprintf(r->stream, "Content-Type: text/html\r\n");
    fprintf(r->stream, "\r\n");

    /* Write HTML Description of Error*/
    fprintf(r->stream, "<body>\n");
    fprintf(r->stream, "<h1>%s</h1>\n", status_string);
    fprintf(r->stream, "<h3>Congrats, you broke our final project.</h3>\n");
    fprintf(r->stream, "</body>\n");

    /* Return specified status */
    return status;
}

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
