/*
 * Action management functions.
 *
 * Copyright 2017 HAProxy Technologies, Christopher Faulet <cfaulet@haproxy.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <haproxy/action.h>
#include <haproxy/base.h>
#include <haproxy/obj_type.h>
#include <haproxy/pool.h>
#include <haproxy/list.h>
#include <haproxy/log.h>
#include <haproxy/proxy.h>
#include <haproxy/task.h>
#include <haproxy/tools.h>

#include <haproxy/stick_table.h>


/* Find and check the target table used by an action track-sc*. This
 * function should be called during the configuration validity check.
 *
 * The function returns 1 in success case, otherwise, it returns 0 and err is
 * filled.
 */
int check_trk_action(struct act_rule *rule, struct proxy *px, char **err)
{
	struct stktable *target;

	if (rule->arg.trk_ctr.table.n)
		target = stktable_find_by_name(rule->arg.trk_ctr.table.n);
	else
		target = px->table;

	if (!target) {
		memprintf(err, "unable to find table '%s' referenced by track-sc%d",
			  rule->arg.trk_ctr.table.n ?  rule->arg.trk_ctr.table.n : px->id,
			  rule->action);
		return 0;
	}

	if (!stktable_compatible_sample(rule->arg.trk_ctr.expr,  target->type)) {
		memprintf(err, "stick-table '%s' uses a type incompatible with the 'track-sc%d' rule",
			  rule->arg.trk_ctr.table.n ? rule->arg.trk_ctr.table.n : px->id,
			  rule->action);
		return 0;
	}
	else if (target->proxy && (px->bind_proc & ~target->proxy->bind_proc)) {
		memprintf(err, "stick-table '%s' referenced by 'track-sc%d' rule not present on all processes covered by proxy '%s'",
			  target->id, rule->action, px->id);
		return 0;
	}
	else {
		if (!in_proxies_list(target->proxies_list, px)) {
			px->next_stkt_ref = target->proxies_list;
			target->proxies_list = px;
		}
		free(rule->arg.trk_ctr.table.n);
		rule->arg.trk_ctr.table.t = target;
		/* Note: if we decide to enhance the track-sc syntax, we may be
		 * able to pass a list of counters to track and allocate them
		 * right here using stktable_alloc_data_type().
		 */
	}

	if (rule->from == ACT_F_TCP_REQ_CNT && (px->cap & PR_CAP_FE) && !px->tcp_req.inspect_delay &&
	    !(rule->arg.trk_ctr.expr->fetch->val & SMP_VAL_FE_SES_ACC)) {
		ha_warning("config : %s '%s' : a 'tcp-request content track-sc*' rule explicitly depending on request"
			   " contents without any 'tcp-request inspect-delay' setting."
			   " This means that this rule will randomly find its contents. This can be fixed by"
			   " setting the tcp-request inspect-delay.\n",
			   proxy_type_str(px), px->id);
	}

	return 1;
}

/* check a capture rule. This function should be called during the configuration
 * validity check.
 *
 * The function returns 1 in success case, otherwise, it returns 0 and err is
 * filled.
 */
int check_capture(struct act_rule *rule, struct proxy *px, char **err)
{
	if (rule->from == ACT_F_TCP_REQ_CNT && (px->cap & PR_CAP_FE) && !px->tcp_req.inspect_delay &&
	    !(rule->arg.trk_ctr.expr->fetch->val & SMP_VAL_FE_SES_ACC)) {
		ha_warning("config : %s '%s' : a 'tcp-request capture' rule explicitly depending on request"
			   " contents without any 'tcp-request inspect-delay' setting."
			   " This means that this rule will randomly find its contents. This can be fixed by"
			   " setting the tcp-request inspect-delay.\n",
			   proxy_type_str(px), px->id);
	}

	return 1;
}

int act_resolution_cb(struct dns_requester *requester, struct dns_nameserver *nameserver)
{
	struct stream *stream;

	if (requester->resolution == NULL)
		return 0;

	stream = objt_stream(requester->owner);
	if (stream == NULL)
		return 0;

	task_wakeup(stream->task, TASK_WOKEN_MSG);

	return 0;
}

int act_resolution_error_cb(struct dns_requester *requester, int error_code)
{
	struct stream *stream;

	if (requester->resolution == NULL)
		return 0;

	stream = objt_stream(requester->owner);
	if (stream == NULL)
		return 0;

	task_wakeup(stream->task, TASK_WOKEN_MSG);

	return 0;
}

