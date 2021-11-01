/*
 * Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 *
 * Authors:
 * 	Casey Schaufler <casey@schaufler-ca.com>
 * 	Ahmed S. Darwish <darwish.07@gmail.com>
 *
 * Special thanks to the authors of selinuxfs.
 *
 *	Karl MacMillan <kmacmillan@tresys.com>
 *	James Morris <jmorris@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/audit.h>
#include "smack.h"

/*
 * smackfs pseudo filesystem.
 */

enum smk_inos {
	SMK_ROOT_INO	= 2,
	SMK_LOAD	= 3,	/* load policy */
	SMK_CIPSO	= 4,	/* load label -> CIPSO mapping */
	SMK_DOI		= 5,	/* CIPSO DOI */
	SMK_DIRECT	= 6,	/* CIPSO level indicating direct label */
	SMK_AMBIENT	= 7,	/* internet ambient label */
	SMK_NETLBLADDR	= 8,	/* single label hosts */
	SMK_ONLYCAP	= 9,	/* the only "capable" label */
	SMK_LOGGING	= 10,	/* logging */
	SMK_LOAD_SELF	= 11,	/* task specific rules */
	SMK_ACCESSES	= 12,	/* access policy */
};

/*
 * List locks
 */
static DEFINE_MUTEX(smack_list_lock);
static DEFINE_MUTEX(smack_cipso_lock);
static DEFINE_MUTEX(smack_ambient_lock);
static DEFINE_MUTEX(smk_netlbladdr_lock);

/*
 * This is the "ambient" label for network traffic.
 * If it isn't somehow marked, use this.
 * It can be reset via smackfs/ambient
 */
char *smack_net_ambient = smack_known_floor.smk_known;

/*
 * This is the level in a CIPSO header that indicates a
 * smack label is contained directly in the category set.
 * It can be reset via smackfs/direct
 */
int smack_cipso_direct = SMACK_CIPSO_DIRECT_DEFAULT;

/*
 * Unless a process is running with this label even
 * having CAP_MAC_OVERRIDE isn't enough to grant
 * privilege to violate MAC policy. If no label is
 * designated (the NULL case) capabilities apply to
 * everyone. It is expected that the hat (^) label
 * will be used if any label is used.
 */
char *smack_onlycap;

/*
 * Certain IP addresses may be designated as single label hosts.
 * Packets are sent there unlabeled, but only from tasks that
 * can write to the specified label.
 */

LIST_HEAD(smk_netlbladdr_list);

/*
 * Rule lists are maintained for each label.
 * This master list is just for reading /smack/load.
 */
struct smack_master_list {
	struct list_head	list;
	struct smack_rule	*smk_rule;
};

LIST_HEAD(smack_rule_list);

static int smk_cipso_doi_value = SMACK_CIPSO_DOI_DEFAULT;

const char *smack_cipso_option = SMACK_CIPSO_OPTION;

/*
 * Values for parsing cipso rules
 * SMK_DIGITLEN: Length of a digit field in a rule.
 * SMK_CIPSOMIN: Minimum possible cipso rule length.
 * SMK_CIPSOMAX: Maximum possible cipso rule length.
 */
#define SMK_DIGITLEN 4
#define SMK_CIPSOMIN (SMK_LABELLEN + 2 * SMK_DIGITLEN)
#define SMK_CIPSOMAX (SMK_CIPSOMIN + SMACK_CIPSO_MAXCATNUM * SMK_DIGITLEN)

/*
 * Values for parsing MAC rules
 * SMK_ACCESS: Maximum possible combination of access permissions
 * SMK_ACCESSLEN: Maximum length for a rule access field
 * SMK_LOADLEN: Smack rule length
 */
#define SMK_OACCESS	"rwxa"
#define SMK_ACCESS	"rwxat"
#define SMK_OACCESSLEN	(sizeof(SMK_OACCESS) - 1)
#define SMK_ACCESSLEN	(sizeof(SMK_ACCESS) - 1)
#define SMK_OLOADLEN	(SMK_LABELLEN + SMK_LABELLEN + SMK_OACCESSLEN)
#define SMK_LOADLEN	(SMK_LABELLEN + SMK_LABELLEN + SMK_ACCESSLEN)

/**
 * smk_netlabel_audit_set - fill a netlbl_audit struct
 * @nap: structure to fill
 */
static void smk_netlabel_audit_set(struct netlbl_audit *nap)
{
	nap->loginuid = audit_get_loginuid(current);
	nap->sessionid = audit_get_sessionid(current);
	nap->secid = smack_to_secid(smk_of_current());
}

/*
 * Values for parsing single label host rules
 * "1.2.3.4 X"
 * "192.168.138.129/32 abcdefghijklmnopqrstuvw"
 */
#define SMK_NETLBLADDRMIN	9
#define SMK_NETLBLADDRMAX	42

/**
 * smk_set_access - add a rule to the rule list
 * @srp: the new rule to add
 * @rule_list: the list of rules
 * @rule_lock: the rule list lock
 *
 * Looks through the current subject/object/access list for
 * the subject/object pair and replaces the access that was
 * there. If the pair isn't found add it with the specified
 * access.
 *
 * Returns 1 if a rule was found to exist already, 0 if it is new
 * Returns 0 if nothing goes wrong or -ENOMEM if it fails
 * during the allocation of the new pair to add.
 */
static int smk_set_access(struct smack_rule *srp, struct list_head *rule_list,
				struct mutex *rule_lock)
{
	struct smack_rule *sp;
	int found = 0;

	mutex_lock(rule_lock);

	/*
	 * Because the object label is less likely to match
	 * than the subject label check it first
	 */
	list_for_each_entry_rcu(sp, rule_list, list) {
		if (sp->smk_object == srp->smk_object &&
		    sp->smk_subject == srp->smk_subject) {
			found = 1;
			sp->smk_access = srp->smk_access;
			break;
		}
	}
	if (found == 0)
		list_add_rcu(&srp->list, rule_list);

	mutex_unlock(rule_lock);

	return found;
}

/**
 * smk_parse_rule - parse Smack rule from load string
 * @data: string to be parsed whose size is SMK_LOADLEN
 * @rule: Smack rule
 * @import: if non-zero, import labels
 */
static int smk_parse_rule(const char *data, struct smack_rule *rule, int import)
{
	char smack[SMK_LABELLEN];
	struct smack_known *skp;

	if (import) {
		rule->smk_subject = smk_import(data, 0);
		if (rule->smk_subject == NULL)
			return -1;

		rule->smk_object = smk_import(data + SMK_LABELLEN, 0);
		if (rule->smk_object == NULL)
			return -1;
	} else {
		smk_parse_smack(data, 0, smack);
		skp = smk_find_entry(smack);
		if (skp == NULL)
			return -1;
		rule->smk_subject = skp->smk_known;

		smk_parse_smack(data + SMK_LABELLEN, 0, smack);
		skp = smk_find_entry(smack);
		if (skp == NULL)
			return -1;
		rule->smk_object = skp->smk_known;
	}

	rule->smk_access = 0;

	switch (data[SMK_LABELLEN + SMK_LABELLEN]) {
	case '-':
		break;
	case 'r':
	case 'R':
		rule->smk_access |= MAY_READ;
		break;
	default:
		return -1;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 1]) {
	case '-':
		break;
	case 'w':
	case 'W':
		rule->smk_access |= MAY_WRITE;
		break;
	default:
		return -1;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 2]) {
	case '-':
		break;
	case 'x':
	case 'X':
		rule->smk_access |= MAY_EXEC;
		break;
	default:
		return -1;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 3]) {
	case '-':
		break;
	case 'a':
	case 'A':
		rule->smk_access |= MAY_APPEND;
		break;
	default:
		return -1;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 4]) {
	case '-':
		break;
	case 't':
	case 'T':
		rule->smk_access |= MAY_TRANSMUTE;
		break;
	default:
		return -1;
	}

	return 0;
}

/**
 * smk_write_load_list - write() for any /smack/load
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start - must be 0
 * @rule_list: the list of rules to write to
 * @rule_lock: lock for the rule list
 *
 * Get one smack access rule from above.
 * The format is exactly:
 *     char subject[SMK_LABELLEN]
 *     char object[SMK_LABELLEN]
 *     char access[SMK_ACCESSLEN]
 *
 * writes must be SMK_LABELLEN+SMK_LABELLEN+SMK_ACCESSLEN bytes.
 */
static ssize_t smk_write_load_list(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos,
				struct list_head *rule_list,
				struct mutex *rule_lock)
{
	struct smack_master_list *smlp;
	struct smack_known *skp;
	struct smack_rule *rule;
	char *data;
	int rc = -EINVAL;
	int load = 0;

	/*
	 * No partial writes.
	 * Enough data must be present.
	 */
	if (*ppos != 0)
		return -EINVAL;
	/*
	 * Minor hack for backward compatibility
	 */
	if (count < (SMK_OLOADLEN) || count > SMK_LOADLEN)
		return -EINVAL;

	data = kzalloc(SMK_LOADLEN, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, count) != 0) {
		rc = -EFAULT;
		goto out;
	}

	/*
	 * More on the minor hack for backward compatibility
	 */
	if (count == (SMK_OLOADLEN))
		data[SMK_OLOADLEN] = '-';

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (rule == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	if (smk_parse_rule(data, rule, 1))
		goto out_free_rule;

	if (rule_list == NULL) {
		load = 1;
		skp = smk_find_entry(rule->smk_subject);
		rule_list = &skp->smk_rules;
		rule_lock = &skp->smk_rules_lock;
	}

	rc = count;
	/*
	 * If this is "load" as opposed to "load-self" and a new rule
	 * it needs to get added for reporting.
	 * smk_set_access returns true if there was already a rule
	 * for the subject/object pair, and false if it was new.
	 */
	if (load && !smk_set_access(rule, rule_list, rule_lock)) {
		smlp = kzalloc(sizeof(*smlp), GFP_KERNEL);
		if (smlp != NULL) {
			smlp->smk_rule = rule;
			list_add_rcu(&smlp->list, &smack_rule_list);
		} else
			rc = -ENOMEM;
		goto out;
	}

out_free_rule:
	kfree(rule);
out:
	kfree(data);
	return rc;
}

/*
 * Core logic for smackfs seq list operations.
 */

static void *smk_seq_start(struct seq_file *s, loff_t *pos,
				struct list_head *head)
{
	struct list_head *list;

	/*
	 * This is 0 the first time through.
	 */
	if (s->index == 0)
		s->private = head;

	if (s->private == NULL)
		return NULL;

	list = s->private;
	if (list_empty(list))
		return NULL;

	if (s->index == 0)
		return list->next;
	return list;
}

static void *smk_seq_next(struct seq_file *s, void *v, loff_t *pos,
				struct list_head *head)
{
	struct list_head *list = v;

	if (list_is_last(list, head)) {
		s->private = NULL;
		return NULL;
	}
	s->private = list->next;
	return list->next;
}

static void smk_seq_stop(struct seq_file *s, void *v)
{
	/* No-op */
}

/*
 * Seq_file read operations for /smack/load
 */

static void *load_seq_start(struct seq_file *s, loff_t *pos)
{
	return smk_seq_start(s, pos, &smack_rule_list);
}

static void *load_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return smk_seq_next(s, v, pos, &smack_rule_list);
}

static int load_seq_show(struct seq_file *s, void *v)
{
	struct list_head *list = v;
	struct smack_master_list *smlp =
		 list_entry(list, struct smack_master_list, list);
	struct smack_rule *srp = smlp->smk_rule;

	seq_printf(s, "%s %s", (char *)srp->smk_subject,
		   (char *)srp->smk_object);

	seq_putc(s, ' ');

	if (srp->smk_access & MAY_READ)
		seq_putc(s, 'r');
	if (srp->smk_access & MAY_WRITE)
		seq_putc(s, 'w');
	if (srp->smk_access & MAY_EXEC)
		seq_putc(s, 'x');
	if (srp->smk_access & MAY_APPEND)
		seq_putc(s, 'a');
	if (srp->smk_access & MAY_TRANSMUTE)
		seq_putc(s, 't');
	if (srp->smk_access == 0)
		seq_putc(s, '-');

	seq_putc(s, '\n');

	return 0;
}

static const struct seq_operations load_seq_ops = {
	.start = load_seq_start,
	.next  = load_seq_next,
	.show  = load_seq_show,
	.stop  = smk_seq_stop,
};

/**
 * smk_open_load - open() for /smack/load
 * @inode: inode structure representing file
 * @file: "load" file pointer
 *
 * For reading, use load_seq_* seq_file reading operations.
 */
static int smk_open_load(struct inode *inode, struct file *file)
{
	return seq_open(file, &load_seq_ops);
}

/**
 * smk_write_load - write() for /smack/load
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start - must be 0
 *
 */
static ssize_t smk_write_load(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{

	/*
	 * Must have privilege.
	 * No partial writes.
	 * Enough data must be present.
	 */
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	return smk_write_load_list(file, buf, count, ppos, NULL, NULL);
}

static const struct file_operations smk_load_ops = {
	.open           = smk_open_load,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_load,
	.release        = seq_release,
};

/**
 * smk_cipso_doi - initialize the CIPSO domain
 */
static void smk_cipso_doi(void)
{
	int rc;
	struct cipso_v4_doi *doip;
	struct netlbl_audit nai;

	smk_netlabel_audit_set(&nai);

	rc = netlbl_cfg_map_del(NULL, PF_INET, NULL, NULL, &nai);
	if (rc != 0)
		printk(KERN_WARNING "%s:%d remove rc = %d\n",
		       __func__, __LINE__, rc);

	doip = kmalloc(sizeof(struct cipso_v4_doi), GFP_KERNEL);
	if (doip == NULL)
		panic("smack:  Failed to initialize cipso DOI.\n");
	doip->map.std = NULL;
	doip->doi = smk_cipso_doi_value;
	doip->type = CIPSO_V4_MAP_PASS;
	doip->tags[0] = CIPSO_V4_TAG_RBITMAP;
	for (rc = 1; rc < CIPSO_V4_TAG_MAXCNT; rc++)
		doip->tags[rc] = CIPSO_V4_TAG_INVALID;

	rc = netlbl_cfg_cipsov4_add(doip, &nai);
	if (rc != 0) {
		printk(KERN_WARNING "%s:%d cipso add rc = %d\n",
		       __func__, __LINE__, rc);
		kfree(doip);
		return;
	}
	rc = netlbl_cfg_cipsov4_map_add(doip->doi, NULL, NULL, NULL, &nai);
	if (rc != 0) {
		printk(KERN_WARNING "%s:%d map add rc = %d\n",
		       __func__, __LINE__, rc);
		kfree(doip);
		return;
	}
}

/**
 * smk_unlbl_ambient - initialize the unlabeled domain
 * @oldambient: previous domain string
 */
static void smk_unlbl_ambient(char *oldambient)
{
	int rc;
	struct netlbl_audit nai;

	smk_netlabel_audit_set(&nai);

	if (oldambient != NULL) {
		rc = netlbl_cfg_map_del(oldambient, PF_INET, NULL, NULL, &nai);
		if (rc != 0)
			printk(KERN_WARNING "%s:%d remove rc = %d\n",
			       __func__, __LINE__, rc);
	}

	rc = netlbl_cfg_unlbl_map_add(smack_net_ambient, PF_INET,
				      NULL, NULL, &nai);
	if (rc != 0)
		printk(KERN_WARNING "%s:%d add rc = %d\n",
		       __func__, __LINE__, rc);
}

/*
 * Seq_file read operations for /smack/cipso
 */

static void *cipso_seq_start(struct seq_file *s, loff_t *pos)
{
	return smk_seq_start(s, pos, &smack_known_list);
}

static void *cipso_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return smk_seq_next(s, v, pos, &smack_known_list);
}

/*
 * Print cipso labels in format:
 * label level[/cat[,cat]]
 */
static int cipso_seq_show(struct seq_file *s, void *v)
{
	struct list_head  *list = v;
	struct smack_known *skp =
		 list_entry(list, struct smack_known, list);
	struct smack_cipso *scp = skp->smk_cipso;
	char *cbp;
	char sep = '/';
	int cat = 1;
	int i;
	unsigned char m;

	if (scp == NULL)
		return 0;

	seq_printf(s, "%s %3d", (char *)&skp->smk_known, scp->smk_level);

	cbp = scp->smk_catset;
	for (i = 0; i < SMK_LABELLEN; i++)
		for (m = 0x80; m != 0; m >>= 1) {
			if (m & cbp[i]) {
				seq_printf(s, "%c%d", sep, cat);
				sep = ',';
			}
			cat++;
		}

	seq_putc(s, '\n');

	return 0;
}

static const struct seq_operations cipso_seq_ops = {
	.start = cipso_seq_start,
	.next  = cipso_seq_next,
	.show  = cipso_seq_show,
	.stop  = smk_seq_stop,
};

/**
 * smk_open_cipso - open() for /smack/cipso
 * @inode: inode structure representing file
 * @file: "cipso" file pointer
 *
 * Connect our cipso_seq_* operations with /smack/cipso
 * file_operations
 */
static int smk_open_cipso(struct inode *inode, struct file *file)
{
	return seq_open(file, &cipso_seq_ops);
}

/**
 * smk_write_cipso - write() for /smack/cipso
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Accepts only one cipso rule per write call.
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_cipso(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct smack_known *skp;
	struct smack_cipso *scp = NULL;
	char mapcatset[SMK_LABELLEN];
	int maplevel;
	int cat;
	int catlen;
	ssize_t rc = -EINVAL;
	char *data = NULL;
	char *rule;
	int ret;
	int i;

	/*
	 * Must have privilege.
	 * No partial writes.
	 * Enough data must be present.
	 */
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	if (*ppos != 0)
		return -EINVAL;
	if (count < SMK_CIPSOMIN || count > SMK_CIPSOMAX)
		return -EINVAL;

	data = kzalloc(count + 1, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, count) != 0) {
		rc = -EFAULT;
		goto unlockedout;
	}

	/* labels cannot begin with a '-' */
	if (data[0] == '-') {
		rc = -EINVAL;
		goto unlockedout;
	}
	data[count] = '\0';
	rule = data;
	/*
	 * Only allow one writer at a time. Writes should be
	 * quite rare and small in any case.
	 */
	mutex_lock(&smack_cipso_lock);

	skp = smk_import_entry(rule, 0);
	if (skp == NULL)
		goto out;

	rule += SMK_LABELLEN;
	ret = sscanf(rule, "%d", &maplevel);
	if (ret != 1 || maplevel > SMACK_CIPSO_MAXLEVEL)
		goto out;

	rule += SMK_DIGITLEN;
	ret = sscanf(rule, "%d", &catlen);
	if (ret != 1 || catlen > SMACK_CIPSO_MAXCATNUM)
		goto out;

	if (count != (SMK_CIPSOMIN + catlen * SMK_DIGITLEN))
		goto out;

	memset(mapcatset, 0, sizeof(mapcatset));

	for (i = 0; i < catlen; i++) {
		rule += SMK_DIGITLEN;
		ret = sscanf(rule, "%d", &cat);
		if (ret != 1 || cat > SMACK_CIPSO_MAXCATVAL)
			goto out;

		smack_catset_bit(cat, mapcatset);
	}

	if (skp->smk_cipso == NULL) {
		scp = kzalloc(sizeof(struct smack_cipso), GFP_KERNEL);
		if (scp == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	}

	spin_lock_bh(&skp->smk_cipsolock);

	if (scp == NULL)
		scp = skp->smk_cipso;
	else
		skp->smk_cipso = scp;

	scp->smk_level = maplevel;
	memcpy(scp->smk_catset, mapcatset, sizeof(mapcatset));

	spin_unlock_bh(&skp->smk_cipsolock);

	rc = count;
out:
	mutex_unlock(&smack_cipso_lock);
unlockedout:
	kfree(data);
	return rc;
}

static const struct file_operations smk_cipso_ops = {
	.open           = smk_open_cipso,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_cipso,
	.release        = seq_release,
};

/*
 * Seq_file read operations for /smack/netlabel
 */

static void *netlbladdr_seq_start(struct seq_file *s, loff_t *pos)
{
	return smk_seq_start(s, pos, &smk_netlbladdr_list);
}

static void *netlbladdr_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return smk_seq_next(s, v, pos, &smk_netlbladdr_list);
}
#define BEBITS	(sizeof(__be32) * 8)

/*
 * Print host/label pairs
 */
static int netlbladdr_seq_show(struct seq_file *s, void *v)
{
	struct list_head *list = v;
	struct smk_netlbladdr *skp =
			 list_entry(list, struct smk_netlbladdr, list);
	unsigned char *hp = (char *) &skp->smk_host.sin_addr.s_addr;
	int maskn;
	u32 temp_mask = be32_to_cpu(skp->smk_mask.s_addr);

	for (maskn = 0; temp_mask; temp_mask <<= 1, maskn++);

	seq_printf(s, "%u.%u.%u.%u/%d %s\n",
		hp[0], hp[1], hp[2], hp[3], maskn, skp->smk_label);

	return 0;
}

static const struct seq_operations netlbladdr_seq_ops = {
	.start = netlbladdr_seq_start,
	.next  = netlbladdr_seq_next,
	.show  = netlbladdr_seq_show,
	.stop  = smk_seq_stop,
};

/**
 * smk_open_netlbladdr - open() for /smack/netlabel
 * @inode: inode structure representing file
 * @file: "netlabel" file pointer
 *
 * Connect our netlbladdr_seq_* operations with /smack/netlabel
 * file_operations
 */
static int smk_open_netlbladdr(struct inode *inode, struct file *file)
{
	return seq_open(file, &netlbladdr_seq_ops);
}

/**
 * smk_netlbladdr_insert
 * @new : netlabel to insert
 *
 * This helper insert netlabel in the smack_netlbladdrs list
 * sorted by netmask length (longest to smallest)
 * locked by &smk_netlbladdr_lock in smk_write_netlbladdr
 *
 */
static void smk_netlbladdr_insert(struct smk_netlbladdr *new)
{
	struct smk_netlbladdr *m, *m_next;

	if (list_empty(&smk_netlbladdr_list)) {
		list_add_rcu(&new->list, &smk_netlbladdr_list);
		return;
	}

	m = list_entry_rcu(smk_netlbladdr_list.next,
			   struct smk_netlbladdr, list);

	/* the comparison '>' is a bit hacky, but works */
	if (new->smk_mask.s_addr > m->smk_mask.s_addr) {
		list_add_rcu(&new->list, &smk_netlbladdr_list);
		return;
	}

	list_for_each_entry_rcu(m, &smk_netlbladdr_list, list) {
		if (list_is_last(&m->list, &smk_netlbladdr_list)) {
			list_add_rcu(&new->list, &m->list);
			return;
		}
		m_next = list_entry_rcu(m->list.next,
					struct smk_netlbladdr, list);
		if (new->smk_mask.s_addr > m_next->smk_mask.s_addr) {
			list_add_rcu(&new->list, &m->list);
			return;
		}
	}
}


/**
 * smk_write_netlbladdr - write() for /smack/netlabel
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Accepts only one netlbladdr per write call.
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_netlbladdr(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct smk_netlbladdr *skp;
	struct sockaddr_in newname;
	char smack[SMK_LABELLEN];
	char *sp;
	char data[SMK_NETLBLADDRMAX + 1];
	char *host = (char *)&newname.sin_addr.s_addr;
	int rc;
	struct netlbl_audit audit_info;
	struct in_addr mask;
	unsigned int m;
	int found;
	u32 mask_bits = (1<<31);
	__be32 nsa;
	u32 temp_mask;

	/*
	 * Must have privilege.
	 * No partial writes.
	 * Enough data must be present.
	 * "<addr/mask, as a.b.c.d/e><space><label>"
	 * "<addr, as a.b.c.d><space><label>"
	 */
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	if (*ppos != 0)
		return -EINVAL;
	if (count < SMK_NETLBLADDRMIN || count > SMK_NETLBLADDRMAX)
		return -EINVAL;
	if (copy_from_user(data, buf, count) != 0)
		return -EFAULT;

	data[count] = '\0';

	rc = sscanf(data, "%hhd.%hhd.%hhd.%hhd/%d %s",
		&host[0], &host[1], &host[2], &host[3], &m, smack);
	if (rc != 6) {
		rc = sscanf(data, "%hhd.%hhd.%hhd.%hhd %s",
			&host[0], &host[1], &host[2], &host[3], smack);
		if (rc != 5)
			return -EINVAL;
		m = BEBITS;
	}
	if (m > BEBITS)
		return -EINVAL;

	/* if smack begins with '-', its an option, don't import it */
	if (smack[0] != '-') {
		sp = smk_import(smack, 0);
		if (sp == NULL)
			return -EINVAL;
	} else {
		/* check known options */
		if (strcmp(smack, smack_cipso_option) == 0)
			sp = (char *)smack_cipso_option;
		else
			return -EINVAL;
	}

	for (temp_mask = 0; m > 0; m--) {
		temp_mask |= mask_bits;
		mask_bits >>= 1;
	}
	mask.s_addr = cpu_to_be32(temp_mask);

	newname.sin_addr.s_addr &= mask.s_addr;
	/*
	 * Only allow one writer at a time. Writes should be
	 * quite rare and small in any case.
	 */
	mutex_lock(&smk_netlbladdr_lock);

	nsa = newname.sin_addr.s_addr;
	/* try to find if the prefix is already in the list */
	found = 0;
	list_for_each_entry_rcu(skp, &smk_netlbladdr_list, list) {
		if (skp->smk_host.sin_addr.s_addr == nsa &&
		    skp->smk_mask.s_addr == mask.s_addr) {
			found = 1;
			break;
		}
	}
	smk_netlabel_audit_set(&audit_info);

	if (found == 0) {
		skp = kzalloc(sizeof(*skp), GFP_KERNEL);
		if (skp == NULL)
			rc = -ENOMEM;
		else {
			rc = 0;
			skp->smk_host.sin_addr.s_addr = newname.sin_addr.s_addr;
			skp->smk_mask.s_addr = mask.s_addr;
			skp->smk_label = sp;
			smk_netlbladdr_insert(skp);
		}
	} else {
		/* we delete the unlabeled entry, only if the previous label
		 * wasn't the special CIPSO option */
		if (skp->smk_label != smack_cipso_option)
			rc = netlbl_cfg_unlbl_static_del(&init_net, NULL,
					&skp->smk_host.sin_addr, &skp->smk_mask,
					PF_INET, &audit_info);
		else
			rc = 0;
		skp->smk_label = sp;
	}

	/*
	 * Now tell netlabel about the single label nature of
	 * this host so that incoming packets get labeled.
	 * but only if we didn't get the special CIPSO option
	 */
	if (rc == 0 && sp != smack_cipso_option)
		rc = netlbl_cfg_unlbl_static_add(&init_net, NULL,
			&skp->smk_host.sin_addr, &skp->smk_mask, PF_INET,
			smack_to_secid(skp->smk_label), &audit_info);

	if (rc == 0)
		rc = count;

	mutex_unlock(&smk_netlbladdr_lock);

	return rc;
}

static const struct file_operations smk_netlbladdr_ops = {
	.open           = smk_open_netlbladdr,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_netlbladdr,
	.release        = seq_release,
};

/**
 * smk_read_doi - read() for /smack/doi
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_doi(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	char temp[80];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d", smk_cipso_doi_value);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	return rc;
}

/**
 * smk_write_doi - write() for /smack/doi
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_doi(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	char temp[80];
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= sizeof(temp) || count == 0)
		return -EINVAL;

	if (copy_from_user(temp, buf, count) != 0)
		return -EFAULT;

	temp[count] = '\0';

	if (sscanf(temp, "%d", &i) != 1)
		return -EINVAL;

	smk_cipso_doi_value = i;

	smk_cipso_doi();

	return count;
}

static const struct file_operations smk_doi_ops = {
	.read		= smk_read_doi,
	.write		= smk_write_doi,
	.llseek		= default_llseek,
};

/**
 * smk_read_direct - read() for /smack/direct
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_direct(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char temp[80];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d", smack_cipso_direct);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	return rc;
}

/**
 * smk_write_direct - write() for /smack/direct
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_direct(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char temp[80];
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= sizeof(temp) || count == 0)
		return -EINVAL;

	if (copy_from_user(temp, buf, count) != 0)
		return -EFAULT;

	temp[count] = '\0';

	if (sscanf(temp, "%d", &i) != 1)
		return -EINVAL;

	smack_cipso_direct = i;

	return count;
}

static const struct file_operations smk_direct_ops = {
	.read		= smk_read_direct,
	.write		= smk_write_direct,
	.llseek		= default_llseek,
};

/**
 * smk_read_ambient - read() for /smack/ambient
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @cn: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_ambient(struct file *filp, char __user *buf,
				size_t cn, loff_t *ppos)
{
	ssize_t rc;
	int asize;

	if (*ppos != 0)
		return 0;
	/*
	 * Being careful to avoid a problem in the case where
	 * smack_net_ambient gets changed in midstream.
	 */
	mutex_lock(&smack_ambient_lock);

	asize = strlen(smack_net_ambient) + 1;

	if (cn >= asize)
		rc = simple_read_from_buffer(buf, cn, ppos,
					     smack_net_ambient, asize);
	else
		rc = -EINVAL;

	mutex_unlock(&smack_ambient_lock);

	return rc;
}

/**
 * smk_write_ambient - write() for /smack/ambient
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_ambient(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char in[SMK_LABELLEN];
	char *oldambient;
	char *smack;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= SMK_LABELLEN)
		return -EINVAL;

	if (copy_from_user(in, buf, count) != 0)
		return -EFAULT;

	smack = smk_import(in, count);
	if (smack == NULL)
		return -EINVAL;

	mutex_lock(&smack_ambient_lock);

	oldambient = smack_net_ambient;
	smack_net_ambient = smack;
	smk_unlbl_ambient(oldambient);

	mutex_unlock(&smack_ambient_lock);

	return count;
}

static const struct file_operations smk_ambient_ops = {
	.read		= smk_read_ambient,
	.write		= smk_write_ambient,
	.llseek		= default_llseek,
};

/**
 * smk_read_onlycap - read() for /smack/onlycap
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @cn: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_onlycap(struct file *filp, char __user *buf,
				size_t cn, loff_t *ppos)
{
	char *smack = "";
	ssize_t rc = -EINVAL;
	int asize;

	if (*ppos != 0)
		return 0;

	if (smack_onlycap != NULL)
		smack = smack_onlycap;

	asize = strlen(smack) + 1;

	if (cn >= asize)
		rc = simple_read_from_buffer(buf, cn, ppos, smack, asize);

	return rc;
}

/**
 * smk_write_onlycap - write() for /smack/onlycap
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_onlycap(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char in[SMK_LABELLEN];
	char *sp = smk_of_task(current->cred->security);

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	/*
	 * This can be done using smk_access() but is done
	 * explicitly for clarity. The smk_access() implementation
	 * would use smk_access(smack_onlycap, MAY_WRITE)
	 */
	if (smack_onlycap != NULL && smack_onlycap != sp)
		return -EPERM;

	if (count >= SMK_LABELLEN)
		return -EINVAL;

	if (copy_from_user(in, buf, count) != 0)
		return -EFAULT;

	/*
	 * Should the null string be passed in unset the onlycap value.
	 * This seems like something to be careful with as usually
	 * smk_import only expects to return NULL for errors. It
	 * is usually the case that a nullstring or "\n" would be
	 * bad to pass to smk_import but in fact this is useful here.
	 */
	smack_onlycap = smk_import(in, count);

	return count;
}

static const struct file_operations smk_onlycap_ops = {
	.read		= smk_read_onlycap,
	.write		= smk_write_onlycap,
	.llseek		= default_llseek,
};

/**
 * smk_read_logging - read() for /smack/logging
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @cn: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_logging(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	char temp[32];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d\n", log_policy);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));
	return rc;
}

/**
 * smk_write_logging - write() for /smack/logging
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_logging(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char temp[32];
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= sizeof(temp) || count == 0)
		return -EINVAL;

	if (copy_from_user(temp, buf, count) != 0)
		return -EFAULT;

	temp[count] = '\0';

	if (sscanf(temp, "%d", &i) != 1)
		return -EINVAL;
	if (i < 0 || i > 3)
		return -EINVAL;
	log_policy = i;
	return count;
}



static const struct file_operations smk_logging_ops = {
	.read		= smk_read_logging,
	.write		= smk_write_logging,
	.llseek		= default_llseek,
};

/*
 * Seq_file read operations for /smack/load-self
 */

static void *load_self_seq_start(struct seq_file *s, loff_t *pos)
{
	struct task_smack *tsp = current_security();

	return smk_seq_start(s, pos, &tsp->smk_rules);
}

static void *load_self_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct task_smack *tsp = current_security();

	return smk_seq_next(s, v, pos, &tsp->smk_rules);
}

static int load_self_seq_show(struct seq_file *s, void *v)
{
	struct list_head *list = v;
	struct smack_rule *srp =
		 list_entry(list, struct smack_rule, list);

	seq_printf(s, "%s %s", (char *)srp->smk_subject,
		   (char *)srp->smk_object);

	seq_putc(s, ' ');

	if (srp->smk_access & MAY_READ)
		seq_putc(s, 'r');
	if (srp->smk_access & MAY_WRITE)
		seq_putc(s, 'w');
	if (srp->smk_access & MAY_EXEC)
		seq_putc(s, 'x');
	if (srp->smk_access & MAY_APPEND)
		seq_putc(s, 'a');
	if (srp->smk_access & MAY_TRANSMUTE)
		seq_putc(s, 't');
	if (srp->smk_access == 0)
		seq_putc(s, '-');

	seq_putc(s, '\n');

	return 0;
}

static const struct seq_operations load_self_seq_ops = {
	.start = load_self_seq_start,
	.next  = load_self_seq_next,
	.show  = load_self_seq_show,
	.stop  = smk_seq_stop,
};


/**
 * smk_open_load_self - open() for /smack/load-self
 * @inode: inode structure representing file
 * @file: "load" file pointer
 *
 * For reading, use load_seq_* seq_file reading operations.
 */
static int smk_open_load_self(struct inode *inode, struct file *file)
{
	return seq_open(file, &load_self_seq_ops);
}

/**
 * smk_write_load_self - write() for /smack/load-self
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start - must be 0
 *
 */
static ssize_t smk_write_load_self(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct task_smack *tsp = current_security();

	return smk_write_load_list(file, buf, count, ppos, &tsp->smk_rules,
					&tsp->smk_rules_lock);
}

static const struct file_operations smk_load_self_ops = {
	.open           = smk_open_load_self,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_load_self,
	.release        = seq_release,
};

/**
 * smk_write_access - handle access check transaction
 * @file: file pointer
 * @buf: data from user space
 * @count: bytes sent
 * @ppos: where to start - must be 0
 */
static ssize_t smk_write_access(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct smack_rule rule;
	char *data;
	int res;

	data = simple_transaction_get(file, buf, count);
	if (IS_ERR(data))
		return PTR_ERR(data);

	if (count < SMK_LOADLEN || smk_parse_rule(data, &rule, 0))
		return -EINVAL;

	res = smk_access(rule.smk_subject, rule.smk_object, rule.smk_access,
			  NULL);
	data[0] = res == 0 ? '1' : '0';
	data[1] = '\0';

	simple_transaction_set(file, 2);
	return SMK_LOADLEN;
}

static const struct file_operations smk_access_ops = {
	.write		= smk_write_access,
	.read		= simple_transaction_read,
	.release	= simple_transaction_release,
	.llseek		= generic_file_llseek,
};

/**
 * smk_fill_super - fill the /smackfs superblock
 * @sb: the empty superblock
 * @data: unused
 * @silent: unused
 *
 * Fill in the well known entries for /smack
 *
 * Returns 0 on success, an error code on failure
 */
static int smk_fill_super(struct super_block *sb, void *data, int silent)
{
	int rc;
	struct inode *root_inode;

	static struct tree_descr smack_files[] = {
		[SMK_LOAD] = {
			"load", &smk_load_ops, S_IRUGO|S_IWUSR},
		[SMK_CIPSO] = {
			"cipso", &smk_cipso_ops, S_IRUGO|S_IWUSR},
		[SMK_DOI] = {
			"doi", &smk_doi_ops, S_IRUGO|S_IWUSR},
		[SMK_DIRECT] = {
			"direct", &smk_direct_ops, S_IRUGO|S_IWUSR},
		[SMK_AMBIENT] = {
			"ambient", &smk_ambient_ops, S_IRUGO|S_IWUSR},
		[SMK_NETLBLADDR] = {
			"netlabel", &smk_netlbladdr_ops, S_IRUGO|S_IWUSR},
		[SMK_ONLYCAP] = {
			"onlycap", &smk_onlycap_ops, S_IRUGO|S_IWUSR},
		[SMK_LOGGING] = {
			"logging", &smk_logging_ops, S_IRUGO|S_IWUSR},
		[SMK_LOAD_SELF] = {
			"load-self", &smk_load_self_ops, S_IRUGO|S_IWUGO},
		[SMK_ACCESSES] = {
			"access", &smk_access_ops, S_IRUGO|S_IWUGO},
		/* last one */
			{""}
	};

	rc = simple_fill_super(sb, SMACK_MAGIC, smack_files);
	if (rc != 0) {
		printk(KERN_ERR "%s failed %d while creating inodes\n",
			__func__, rc);
		return rc;
	}

	root_inode = sb->s_root->d_inode;
	root_inode->i_security = new_inode_smack(smack_known_floor.smk_known);

	return 0;
}

/**
 * smk_mount - get the smackfs superblock
 * @fs_type: passed along without comment
 * @flags: passed along without comment
 * @dev_name: passed along without comment
 * @data: passed along without comment
 *
 * Just passes everything along.
 *
 * Returns what the lower level code does.
 */
static struct dentry *smk_mount(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, smk_fill_super);
}

static struct file_system_type smk_fs_type = {
	.name		= "smackfs",
	.mount		= smk_mount,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *smackfs_mount;

/**
 * init_smk_fs - get the smackfs superblock
 *
 * register the smackfs
 *
 * Do not register smackfs if Smack wasn't enabled
 * on boot. We can not put this method normally under the
 * smack_init() code path since the security subsystem get
 * initialized before the vfs caches.
 *
 * Returns true if we were not chosen on boot or if
 * we were chosen and filesystem registration succeeded.
 */
static int __init init_smk_fs(void)
{
	int err;

	if (!security_module_enable(&smack_ops))
		return 0;

	err = register_filesystem(&smk_fs_type);
	if (!err) {
		smackfs_mount = kern_mount(&smk_fs_type);
		if (IS_ERR(smackfs_mount)) {
			printk(KERN_ERR "smackfs:  could not mount!\n");
			err = PTR_ERR(smackfs_mount);
			smackfs_mount = NULL;
		}
	}

	smk_cipso_doi();
	smk_unlbl_ambient(NULL);

	return err;
}

__initcall(init_smk_fs);
