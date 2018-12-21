// Copyright 2018 Schibsted

#include "sbp/stat_messages.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sbp/memalloc_functions.h"

STAT_MESSAGE_DECLARE(foo, "x", "a");
STAT_MESSAGE_DECLARE(bar, "y", "b");
STAT_MESSAGE_DECLARE(multi, "quite", "many");

static void
stat_dump(void *cb_data, const char *msg, const char **name) {
	while (*name) {
		printf("%s ", *(name++));
	}
	printf("%s\n", msg?:"NULL");
}

int
main(int argc, char **argv) {
	struct stat_message *dynmsg1;
	struct stat_message *dynmsg2;

	printf("Set 'x a'\n");
	stat_message_printf(&foo, "First value");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nOverwrite 'x a'\n");
	stat_message_printf(&foo, "Second value");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nSet 'y b'\n");
	stat_message_printf(&bar, "A million mysterious monkeys meticulously making merry make-magic");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nSet using PRINTF 'y b'\n");
	stat_message_printf(&bar, "Splendid %s", "spoons");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nOverwrite using PRINTF 'y b'\n");
	stat_message_printf(&bar, "Think %s", "Blue");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nOverwrite SET using PRINTF 'x a'\n");
	stat_message_printf(&foo, "Count %s", "Two");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nPRINTF without arguments 'x a'\n");
	stat_message_printf(&foo, "No args");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nAllocating one dynamic message\n");
	dynmsg1 = stat_message_dynamic_alloc(3, "dynamic", "message", "one");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nSet dynamic message\n");
	stat_message_printf(dynmsg1, "%s dynamic message", "First");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nOverwrite dynamic message\n");
	stat_message_printf(dynmsg1, "%s dynamic message", "Second");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nAllocating another dynamic message\n");
	dynmsg2 = stat_message_dynamic_alloc(3, "dynamic", "message", "two");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nSet second dynamic message\n");
	stat_message_printf(dynmsg2, "%s dynamic message", "Another");
	stat_messages_foreach(stat_dump, NULL);

	printf("\nFree first dynamic message\n");
	stat_message_dynamic_free(dynmsg1);
	stat_messages_foreach(stat_dump, NULL);

	printf("\nFree second dynamic message\n");
	stat_message_dynamic_free(dynmsg2);
	stat_messages_foreach(stat_dump, NULL);
	return 0;
}
