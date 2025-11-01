#include "../csapp.h"

int main(void) {
    char *query = getenv("QUERY_STRING");

    printf("Content-type: text/html\r\n\r\n");
    printf("<html><body>\n");
    printf("<h2>Echo CGI Program</h2>\n");

    if (query)
        printf("<p>Query string: %s</p>\n", query);
    else
        printf("<p>No query received</p>\n");

    printf("</body></html>\n");
    return 0;
}
