#include "suggestion.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int starts_with(const char *text, const char *prefix, size_t prefix_len) {
  return strncmp(text, prefix, prefix_len) == 0;
}

static int contains_match(const CallSuggestionList *list, const char *call) {
  if (!list || !call)
    return 0;

  for (int i = 0; i < list->count; i++) {
    if (strcmp(list->matches[i], call) == 0)
      return 1;
  }

  return 0;
}

static int extract_prefix_token(const char *input, char *out, size_t out_size,
                                size_t *token_len) {
  if (!input || !out || out_size < 2)
    return 0;

  out[0] = 0;
  if (token_len)
    *token_len = 0;

  size_t len = strlen(input);
  size_t start = 0;

  while (start < len && isspace((unsigned char)input[start]))
    start++;

  if (start >= len)
    return 0;

  size_t end = start;
  while (end < len && !isspace((unsigned char)input[end]) && input[end] != ';')
    end++;

  if (end < len && isspace((unsigned char)input[end]))
    return 0;

  size_t n = end - start;
  if (n < 1 || n >= out_size)
    return 0;

  for (size_t i = 0; i < n; i++)
    out[i] = (char)toupper((unsigned char)input[start + i]);

  out[n] = 0;

  if (token_len)
    *token_len = n;

  return 1;
}

void call_suggestion_list_clear(CallSuggestionList *list) {
  if (!list)
    return;

  memset(list, 0, sizeof(*list));
}

void call_suggestion_refresh(CallSuggestionList *list, const char *input,
                             char history[][CALL_SUGGESTION_LEN],
                             int history_count) {
  if (!list)
    return;

  call_suggestion_list_clear(list);

  if (!input || !history || history_count <= 0)
    return;

  char prefix[CALL_SUGGESTION_LEN] = {0};
  size_t prefix_len = 0;

  if (!extract_prefix_token(input, prefix, sizeof(prefix), &prefix_len))
    return;

  for (int i = history_count - 1; i >= 0; i--) {
    if (!history[i][0])
      continue;

    if (!starts_with(history[i], prefix, prefix_len))
      continue;

    if (strcmp(history[i], prefix) == 0)
      continue;

    if (contains_match(list, history[i]))
      continue;

    snprintf(list->matches[list->count], CALL_SUGGESTION_LEN, "%s", history[i]);
    list->count++;

    if (list->count >= CALL_SUGGESTION_MAX)
      break;
  }

  list->selected = 0;
}

const char *call_suggestion_selected(const CallSuggestionList *list) {
  if (!list || list->count <= 0)
    return NULL;

  if (list->selected < 0 || list->selected >= list->count)
    return NULL;

  return list->matches[list->selected];
}

void call_suggestion_select_prev(CallSuggestionList *list) {
  if (!list || list->count <= 0)
    return;

  if (list->selected <= 0)
    list->selected = list->count - 1;
  else
    list->selected--;
}

void call_suggestion_select_next(CallSuggestionList *list) {
  if (!list || list->count <= 0)
    return;

  list->selected++;
  if (list->selected >= list->count)
    list->selected = 0;
}

int call_suggestion_apply(const CallSuggestionList *list, char *input, int *len,
                          size_t input_capacity) {
  if (!list || !input || !len || input_capacity < 2)
    return 0;

  const char *selected = call_suggestion_selected(list);
  if (!selected)
    return 0;

  size_t in_len = strlen(input);
  size_t start = 0;

  while (start < in_len && isspace((unsigned char)input[start]))
    start++;

  size_t end = start;
  while (end < in_len && !isspace((unsigned char)input[end]) && input[end] != ';')
    end++;

  size_t selected_len = strlen(selected);
  size_t suffix_len = in_len - end;

  if (start + selected_len + suffix_len >= input_capacity)
    return 0;

  memmove(input + start + selected_len, input + end, suffix_len + 1);
  memcpy(input + start, selected, selected_len);

  *len = (int)strlen(input);

  return 1;
}
