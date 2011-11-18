const char *pup_type_name(const struct PupClass *type);

PupMethod *find_method_in_classes(struct PupClass *class,
                                  const long name_sym);

bool pup_is_descendant_or_same(const struct PupClass *ancestor,
                              const struct PupClass *descendant);

bool pup_is_class_instance(const struct PupObject *obj);
