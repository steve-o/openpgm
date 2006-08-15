/* vim:ts=4:sts=4:sw=2:noai:noexpandtab
 * 
 * PGM packet formats, RFC 3208.
 *
 * Copyright (c) 2006 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* 8.1.  Source Path Messages (SPM) */
struct pgm_spm {
	u_int16_t	spm_sport;			/* source port */
	u_int16_t	spm_dport;			/* destination port */
	u_int8_t	spm_type;			/* type */
	u_int8_t	spm_options;		/* options */
	u_int16_t	spm_checksum;		/* checksum */
	u_int8_t	spm_gsi[6];			/* global source id */
	u_int16_t	spm_tsdu_length;	/* tsdu length */
	u_int32_t	spm_sqn;			/* spm sequence number */
	u_int32_t	spm_trail;			/* trailing edge sequence number */
	u_int32_t	spm_lead;			/* leading edge sequence number */
	u_int16_t	spm_nla_afi;		/* nla afi */
	u_int16_t	spm_reserved;		/* reserved */
	/* ... path nla */
	/* ... option extensions */
};

/* 8.2.  Data Packet */
struct pgm_data {
	union {
	u_int16_t	od_sport;			/* source port */
	u_int16_t	rd_sport;
	};
	union {
	u_int16_t	od_dport;			/* destination port */
	u_int16_t	rd_dport;
	};
	union {
	u_int8_t	od_type;			/* type */
	u_int8_t	rd_type;
	};
	union {
	u_int8_t	od_options;			/* options */
	u_int8_t	rd_options;
	};
	union {
	u_int16_t	od_checksum;		/* checksum */
	u_int16_t	rd_checksum;
	};
	union {
	u_int8_t	od_gsi[6];			/* global source id */
	u_int8_t	rd_gsi[6];
	};
	union {
	u_int16_t	od_tsdu_length;		/* tsdu length */
	u_int16_t	rd_tsdu_length;
	};
	union {
	u_int32_t	od_sqn;				/* data packet sequence number */
	u_int32_t	rd_sqn;
	};
	union {
	u_int32_t	od_trail;			/* trailing edge sequence number */
	u_int32_t	rd_trail;
	};
	/* ... option extensions */
	/* ... data */
};

/* 8.3.  Negative Acknowledgments and Confirmations (NAK, N-NAK, & NCF) */
struct pgm_nak {
	union {
	u_int16_t	nak_sport;			/* source port */
	u_int16_t	nnak_sport;
	u_int16_t	ncf_sport;
	};
	union {
	u_int16_t	nak_dport;			/* destination port */
	u_int16_t	nnak_dport;
	u_int16_t	ncf_dport;
	};
	union {
	u_int8_t	nak_type;			/* type */
	u_int8_t	nnak_type;
	u_int8_t	ncf_type;
	};
	u_int8_t	nak_options;		/* options */
	u_int16_t	nak_checksum;		/* checksum */
	u_int8_t	nak_gsi[6];			/* global source id */
	u_int16_t	nak_tsdu_length;	/* tsdu length */
	u_int32_t	nak_sqn;			/* requested sequence number */
	u_int16_t	nak_nla_afi;		/* nla afi */
	u_int16_t	nak_reserved;		/* reserved */
	u_int32_t	nak_src;			/* source nla */
	u_int16_t	nak_nla_afi;		/* nla afi */
	u_int16_t	nak_reserved2;		/* reserved */
	u_int32_t	nak_grp;			/* multicast group nla */
	/* ... option extension */
};

/* 9.  Options */
struct pgm_option {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	/* ... option value */
};

/* 9.1.  Option extension length - OPT_LENGTH */
struct pgm_opt_length {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_total_length;	/* total length of all options */
};

/* 9.2.  Option fragment - OPT_FRAGMENT */
struct pgm_opt_fragment {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	opt_sqn;			/* first sequence number */
	u_int32_t	opt_frag_off;		/* offset */
	u_int32_t	opt_frag_len;		/* length */
};

/* 9.3.5.  Option NAK List - OPT_NAK_LIST */
struct pgm_opt_nak_list {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	opt_sqn;			/* requested sequence number [62] */
};

/* 9.4.2.  Option Join - OPT_JOIN */
struct pgm_opt_join {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	opt_join_min;		/* minimum sequence number */
};

/* 9.5.5.  Option Redirect - OPT_REDIRECT */
struct pgm_opt_redirect {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int16_t	opt_nla_afi;		/* nla afi */
	u_int16_t	opt_reserved2;		/* reserved */
	u_int32_t	opt_nla;			/* dlr nla */
};

/* 9.6.2.  Option Sources - OPT_SYN */
struct pgm_opt_syn {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
};

/* 9.7.4.  Option End Session - OPT_FIN */
struct pgm_opt_fin {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
};

/* 9.8.4.  Option Reset - OPT_RST */
struct pgm_opt_rst {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
};


/*
 * Forward Error Correction - FEC
 */

/* 11.8.1.  Option Parity - OPT_PARITY_PRM */
struct pgm_opt_parity_prm {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	parity_prm_tgs;		/* transmission group size */
};

/* 11.8.2.  Option Parity Group - OPT_PARITY_GRP */
struct pgm_opt_parity_grp {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	prm_group;			/* parity group number */
};

/* 11.8.3.  Option Current Transmission Gropu Size - OPT_CURR_TGSIZE */
struct pgm_opt_curr_tgsize {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	prm_atgsize;		/* actual transmission group size */
};

/*
 * Congestion Control
 */

/* 12.7.1.  Option Congestion Report - OPT_CR */
struct pgm_opt_cr {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	opt_cr_lead;		/* congestion report reference sqn */
	u_int16_t	opt_cr_ne_wl;		/* ne worst link */
	u_int16_t	opt_cr_ne_wp;		/* ne worst path */
	u_int16_t	opt_cr_rx_wp;		/* rcvr worst path */
	u_int16_t	opt_reserved2;		/* reserved */
	u_int16_t	opt_nla_afi;		/* nla afi */
	u_int16_t	opt_reserved3;		/* reserved */
	u_int32_t	opt_cr_rcvr;		/* worst receivers nla */
};

/* 12.7.2.  Option Congestion Report Request - OPT_CRQST */
struct pgm_opt_crqst {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
};


/*
 * SPM Requests
 */

/* 13.6.  SPM Requests */
struct pgm_spmr {
	u_int16_t	spmr_sport;			/* source port */
	u_int16_t	spmr_dport;			/* destination port */
	u_int8_t	spmr_type;			/* type */
	u_int8_t	spmr_options;		/* options */
	u_int16_t	spmr_checksum;		/* checksum */
	u_int8_t	spmr_gsi[6];			/* global source id */
	u_int16_t	spmr_tsdu_length;	/* tsdu length */
	/* ... option extensions */
};


/*
 * Poll Mechanism
 */

/* 14.7.1.  Poll Request */
struct pgm_poll {
	u_int16_t	poll_sport;			/* source port */
	u_int16_t	poll_dport;			/* destination port */
	u_int8_t	poll_type;			/* type */
	u_int8_t	poll_options;		/* options */
	u_int16_t	poll_checksum;		/* checksum */
	u_int8_t	poll_gsi[6];		/* global source id */
	u_int16_t	poll_tsdu_length;	/* tsdu length */
	u_int32_t	poll_sqn;			/* poll sequence number */
	u_int16_t	poll_round;			/* poll round */
	u_int16_t	poll_s_type;		/* poll sub-type */
	u_int16_t	poll_nla_afi;		/* nla afi */
	u_int16_t	poll_reserved;		/* reserved */
	u_int32_t	poll_path;			/* path nla */
	u_int32_t	poll_bo_ivl;		/* poll back-off interval */
	u_int32_t	poll_rand;			/* random string */
	u_int32_t	poll_mask;			/* matching bit-mask */
	/* ... option extensions */
};

/* 14.7.2.  Poll Response */
struct pgm_polr {
	u_int16_t	polr_sport;			/* source port */
	u_int16_t	polr_dport;			/* destination port */
	u_int8_t	polr_type;			/* type */
	u_int8_t	polr_options;		/* options */
	u_int16_t	polr_checksum;		/* checksum */
	u_int8_t	polr_gsi[6];		/* global source id */
	u_int16_t	polr_tsdu_length;	/* tsdu length */
	u_int32_t	polr_sqn;			/* polr sequence number */
	u_int16_t	polr_round;			/* polr round */
	u_int16_t	polr_reserved;		/* reserved */
	/* ... option extensions */
};


/*
 * Implosion Prevention
 */

/* 15.4.1.  Option NAK Back-Off Interval - OPT_NAK_BO_IVL */
struct pgm_opt_nak_bo_ivl {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	nak_bo_ivl;			/* nak back-off interval */
	u_int32_t	nak_bo_ivl_sqn;		/* nak back-off interval sqn */
};

/* 15.4.2.  Option NAK Back-Off Range - OPT_NAK_BO_RNG */
struct pgm_opt_nak_bo_rng {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	nak_max_bo_ivl;		/* maximum nak back-off interval */
	u_int32_t	nak_min_bo_ivl;		/* minimum nak back-off interval */
};

/* 15.4.3.  Option Unreachable - OPT_NBR_UNREACH */
struct pgm_opt_crqst {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
};

/* 15.4.4.  Option Path - OPT_PATH_NLA */
struct pgm_opt_crqst {
	u_int8_t	opt_type;			/* option type */
	u_int8_t	opt_length;			/* option length */
	u_int16_t	opt_reserved;		/* reserved */
	u_int32_t	path_nla;			/* path nla */
};


