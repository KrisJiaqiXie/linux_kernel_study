extern void	pstore_set_kmsg_bytes(int);
extern void	pstore_get_records(int);
extern int	pstore_mkfile(enum pstore_type_id, char *psname, u64 id,
			      char *data, size_t size,
			      struct timespec time, struct pstore_info *psi);
extern int	pstore_is_mounted(void);
