#ifndef SUGGESTION_H
#define SUGGESTION_H

#include <stddef.h>

#define CALL_SUGGESTION_MAX 12
#define CALL_SUGGESTION_LEN 32

typedef struct {
  char matches[CALL_SUGGESTION_MAX][CALL_SUGGESTION_LEN];
  int count;
  int selected;
} CallSuggestionList;

void call_suggestion_list_clear(CallSuggestionList *list);

void call_suggestion_refresh(CallSuggestionList *list, const char *input,
                             char history[][CALL_SUGGESTION_LEN],
                             int history_count);

int call_suggestion_apply(const CallSuggestionList *list, char *input, int *len,
                          size_t input_capacity);

const char *call_suggestion_selected(const CallSuggestionList *list);

void call_suggestion_select_prev(CallSuggestionList *list);
void call_suggestion_select_next(CallSuggestionList *list);

#endif