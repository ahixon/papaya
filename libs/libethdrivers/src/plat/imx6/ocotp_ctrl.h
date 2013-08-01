
struct ocotp;

struct ocotp *ocotp_init(void);

int ocotp_get_mac(struct ocotp* ocotp, unsigned char *mac);

