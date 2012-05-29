/* Copyright (c) 2006-2012, DNSPod Inc.
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: 
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies, 
 * either expressed or implied, of the FreeBSD Project.
 */


#include "dns.h"


//types we support at the moment
const enum rrtype support_type[] = {A,NS,CNAME,SOA,MX,TXT,AAAA,SRV};


////////////////////////////////////////////////////////////////////
//'a','.','b','.','c','.',0
// 1 ,'a','1','b', 1, 'c',0
uchar* str_to_len_label(uchar *domain,int len)
{
 uchar *ptr,l = 0;
 int i;
 //we need a extran byte to put len.
 if(domain[len - 1] != 0 || domain[len - 2] != '.')
	return NULL;
 for(i = len - 2;i > 0;i --)
	{
	 domain[i] = domain[i - 1];
	 l ++;
	 if(domain[i] == '.')
		{
		 domain[i] = l - 1;
		 l = 0;
		} 
	}
 domain[0] = l;
 return domain;
}


//we do not support type DS, KEY etc.
int check_support_type(ushort type)
{
 int i,num = sizeof(support_type) / sizeof(enum rrtype);
 for(i = 0;i < num;i ++)
	 if(type == support_type[i])
		return 0;
 return -1;
}


//make import info into struct baseinfo
struct baseinfo passer_dns_data(struct sockinfo *si)
{
 uchar *buf = si->buf;
 int num;
 int i = 0,len = sizeof(struct sockaddr_in);
 int dlen = 0,seg = 0;
 struct baseinfo bi;
 uchar *tail = NULL,*ptrs[MAX_NS_LVL] = {0},*domain = NULL;
 ushort offset;
 dnsheader *hdr = (dnsheader*)buf;
 bi.err = 1;
 num = ntohs(hdr->qdcount);
 if(num != 1)
	return bi;
 num = ntohs(hdr->ancount);
 if(num != 0)
	return bi;
 num = ntohs(hdr->nscount);
 if(num != 0)
	return bi;
 num = ntohs(hdr->arcount);
 if(num > 1) //edns makes ar==1
	return bi;
 bi.id = hdr->id;
 //if(check_client_addr() < 0)
	//return bi;
 dlen = check_dns_name(buf + sizeof(dnsheader),&seg);
 if(dlen < 0)
	return bi;
 bi.dlen = dlen;
 tail = bi.origindomain = buf + sizeof(dnsheader);
 tail += dlen;
 bi.type = ntohs(*(ushort*)tail);
 if(check_support_type(bi.type) == 0)
	 bi.err = 0;
 return bi;
}


//we'd better send the right domain and id
int send_tc_to_client(uchar *td,struct sockinfo *si,ushort cid)
{
 uchar buffer[255] = {0},*itor = buffer;
 dnsheader *hdr = (dnsheader*)itor;
 qdns *qd = NULL;
 int dlen = -1;
 if(td == NULL || si == NULL)
	return -1;
 hdr->id = cid;
 hdr->flags = 0;
 hdr->flags = SET_QR_R(hdr->flags);
 hdr->flags = SET_RA(hdr->flags);
 hdr->flags = SET_TC(hdr->flags);
 hdr->flags = htons(hdr->flags);
 hdr->qdcount = htons(1);
 hdr->ancount = hdr->nscount = hdr->arcount = htons(0);
 itor += sizeof(dnsheader);
 dlen = strlen(td + 1);
 memcpy(itor,td + 1,dlen + 1);
 itor = itor + dlen + 1;
 qd = (qdns*)itor;
 qd->type = htons(td[0]);
 qd->dclass = htons(CLASS_IN);
 itor += sizeof(qdns);
 si->buf = buffer;
 si->buflen = itor - buffer;
 udp_write_info(si,0);
 return 0;
}


//transfrom domain from lenlabel format to string
int get_domain_from_msg(uchar *itor,uchar *hdr,uchar *to)
{
 uchar len,*tmp = NULL;
 ushort offset = 0;
 len = itor[0]; 
 int dlen = 0,tmplen = 0;
 int hasptr = 0,infinite = 20;
 offset = htons((ushort)*(ushort*)itor);
 while((len != 0) && (infinite --))
	{
 	 if(IS_PTR(offset))
		{
		 itor = hdr + GET_OFFSET(offset);
		 if(hasptr == 0)
			{
			 dlen = 2;
			 if(tmplen != 0)
				dlen += tmplen;
			}
		 hasptr = 1;
		}
	 to[0] = itor[0];
	 tmplen += 1; //len
	 tmplen += to[0]; //label
	 if(to[0] > 64)
		return -1;
	 to ++;
	 memcpy(to,itor + 1,itor[0]);
	 to += itor[0];
	 itor = itor + itor[0] + 1;
	 len = itor[0];
	 offset = htons((ushort)*(ushort*)itor);
	}
 if(infinite <= 0) //loops error
	return -1;
 to[0] = 0;
 to ++;
 tmplen ++;
 if(dlen == 0)
	dlen = tmplen; //root len is 1
 if(dlen > MAX_DOMAIN_LEN)
	return -1;
 return dlen;
}


//malloced here
//tn will be free by author before add_to_quizzer
//and data will be free by release_qoutinfo
int insert_into_ttltree(struct rbtree *rbt,uchar *td,uint ttl)
{
 struct rbnode node = {0};
 struct ttlnode *tn = NULL;
 int len = 0;
 len = strlen(td) + 1;
 //printf("ttl is %u\n",ttl);
 if((tn = malloc(sizeof(struct ttlnode))) == NULL)
	return -1;
 if((tn->data = malloc(len)) == NULL)
	{
	 free(tn);
	 return -1;
	}
 tn->dlen = len;
 tn->exp = ttl;
 memcpy(tn->data,td,len);
 node.key = tn;
 insert_node(rbt,&node);
 return 0;
}


uint random_ttl(uint ttl)
{
 uint tmp,ret = ttl % 7;
 ttl = ttl + ret * 3;
 if(ttl > MAX_TTL)
	ttl = MAX_TTL - (ttl % MAX_TTL);
 return ttl;
}


int is_parent(uchar *parent,uchar *son)
{
 int sp,ss,x;
 sp = strlen(parent);
 ss = strlen(son);
 if(ss < sp)
	return -1;
 x = ss - sp;
 son = son + x;
 if(strcmp(parent,son) == 0)
	 return 0;
 return -1;
}


//if we query abc.com
//the auth server returned
//bbc.com NS ns1.sina.com
//we should reject this
int check_dms(uchar *ck,uchar *dms,int num)
{
 return 0;
}


//when we insert "ttl expired" in the rbtree
//if A or CNAME or MX or TXT or ... 's ttl is small, then we don't need to insert
//NS's "ttl expired" element, we update the record at the same time when we
//update A or CNAME Or MX or TXT or....
//if NS's ttl is small than A or CNAME or MX or TXT or ...
//we update it when update A or CNAME or MX or TXT, or when some query some domain's NS
//in brief, we insert ANSWER section into ttl tree only.
uchar* process_rdata(struct hlpp *hlp,uchar *label,int n)
{
 uchar buffer[65535] = {0};
 ushort type = 0,class,lth,offset;
 uint ttl = 0,tmpttl,tx;
 int i,dlen,bidx = 0,ret;
 int *stype = hlp->stype;
 struct htable *ds = hlp->ds;
 struct rbtree *rbt = hlp->rbt;
 uchar *hdr = hlp->buf;
 int mlen = hlp->datalen;
 struct mvalue *mv = (struct mvalue*)buffer;
 uchar tmpdomain[512] = {0},dm[512] = {0},*itor = NULL;
 ushort tmptype = 0;
 int tag;
 itor = buffer + sizeof(struct mvalue);
 tx = global_now; ///
 //if(hlp->section != AN_SECTION) //see header comments.
	//rbt = NULL;
 for(i = 0;i < n;i ++)
 	{
	 dlen = get_domain_from_msg(label,hdr,tmpdomain);
	 if(dm[1] == 0 && dm[2] == 0) //first time
		{
	 	 memcpy(dm + 1,tmpdomain,strlen(tmpdomain) + 1);
		 if(check_dms(dm + 1,hlp->dms,hlp->dmsidx) < 0)
			return NULL;
		}
	 if(dlen < 0)
		 return NULL;
	 label += dlen;
	 if(get_dns_info(label,&tmptype,&class,&ttl,&lth) < 0)
		 return NULL;
	 if(ttl < MIN_TTL)
		ttl = MIN_TTL;
	 ttl = random_ttl(ttl + n);
	 label += 10; // 2type,2class,4ttl,2lth
	 if(tmptype == SOA || tmptype == CNAME)
		*stype = tmptype;
	 if(type == 0) //first time
		type = tmptype;
	 if(ttl > MAX_TTL)
		ttl = MAX_TTL;
	 if(tmpttl == 0) //first time
		tmpttl = ttl;
	 if((strcmp(tmpdomain,dm + 1) != 0) || (type != tmptype))
		{
		 if(check_dms(dm,hlp->dms,hlp->dmsidx) < 0)
			return NULL;
		 dm[0] = type;
		 mv->ttl = random_ttl(tmpttl + i + (tx % 5)) + tx;
		 mv->hits = 0;
		 mv->seg = 0;
		 //23com0
		 if(dm[dm[1] + 2] != 0) //not top level domain
			 insert_kv_mem(rbt,ds,dm,buffer,mv->len + sizeof(struct mvalue));
		 type = tmptype;
		 memcpy(dm + 1,tmpdomain,strlen(tmpdomain) + 1);
		 mv->len = mv->ttl = mv->num = mv->hits = 0;
		 itor = buffer + sizeof(struct mvalue);
		}
	 ret = fill_rrset_in_buffer(itor,label,hdr,lth,type,hlp);
	 if(ret > 0)
		{
		 itor += ret; //in dns msg
		 mv->len += ret; //in memory
		 mv->num ++;
		}
	 tmpttl = ttl;
	 label += lth;
	 if((label < hdr) || (label > (hdr + mlen)))
		 return NULL;
	}
 if(mv->num > 0)
	{
	 dm[0] = type;
	 mv->ttl = random_ttl(tmpttl + i + (tx % 5)) + tx;
	 mv->hits = 0;
	 mv->seg = 0;
	 if(dm[dm[1] + 2] != 0) //not top level domain
		 insert_kv_mem(rbt,ds,dm,buffer,mv->len + sizeof(struct mvalue));
	}
 return label;
}


int check_domain_mask(uchar *domain,uchar *origin)
{
 int len = 0;
 len = strlen(origin);
 return strncmp(origin,domain,len + 1);
}


int get_dns_info(uchar *label,ushort *tp,ushort *cls,uint *ttl,ushort *lth)
{
 ushort *us = NULL;
 uint *ui = NULL;
 us = (ushort*)label;
 *tp = ntohs(*us); //type
 if(*tp > 254)
	{
	 printf("type is %u\n",*tp);
	 return -1;
	}
 label += sizeof(ushort);
 us = (ushort*)label;
 *cls = ntohs(*us);
 if(*cls != CLASS_IN)
	 return -1;
 label += sizeof(ushort);
 ui = (uint*)label;
 *ttl = ntohl(*ui);
 label += sizeof(uint);
 us = (ushort*)label;
 *lth = ntohs(*us);
 return 0;
}


int check_dns_name(uchar *domain,int *seg)
{
 uchar len = domain[0],i;
 int tlen = 0,sg = 0; //extra total len and type
 domain ++;
 while(len != 0)
	{
	 if(len > 63)
		return -1;
	 for(i = 0;i < len;i ++) //num a-z A-Z -
		if(!((domain[i] >= '0' && domain[i] <= '9') || (domain[i] >= 'a' && domain[i] <= 'z') || (domain[i] >= 'A' && domain[i] <= 'Z') || domain[i] == '-'))
			return -1;
	 sg ++;
	 tlen = tlen + 1 + len;
	 domain = domain + len;
	 len = domain[0];
	 domain ++;
	}
 tlen ++;//for end 0
 if(tlen > 255)
	return -1;
 *seg = sg;
 return tlen;
}


int make_type_domain(uchar *domain,int dlen,int type,uchar *buffer)
{
 if(buffer == NULL || domain == NULL)
	return -1;
 buffer[0] = type;
 memcpy(buffer + 1,domain,dlen + 1);
 return 0;
}


int check_memcpy(uchar *to,uchar *from,int vlen)
{
 int i;
 for(i = 0;i < vlen;i ++)
	if(to[i] != from[i])
		return -1;
 return 0;
}


//k and v both are in stack
//k td
//v mvalue.data
int insert_kv_mem(struct rbtree *rbt,struct htable *ds,uchar *k,uchar *v,int vlen)
{
 uchar *val = NULL;
 struct mvalue *mv = NULL,tmp;
 int ret = -1;
 struct rbnode *pn = NULL;
 struct ttlnode tn = {0};
 if(vlen < 0 || vlen > MAX_RECORD_SIZE)
	return -1;
 hashval_t hash = nocase_char_hash_function(k);
 hash = get_pre_mem_hash(k);
 val = malloc(vlen);
 if(val == NULL)
	return -1;
 memcpy(val,v,vlen);
 mv = (struct mvalue*)v;
 ret = htable_insert(ds + hash,k,val,1,&tmp); //mem, replace
 if(ret == 2)
	free(val);
 if(mv->ttl == (MAX_TTL + 1))//never expired
	 return 0;
 if(rbt == NULL)
	 return 0;
 //data exists in htable, delete it in ttl tree, then insert
 pthread_mutex_lock(&rbt->lock);
 if(ret != 0)
	{
	 tn.dlen = strlen(k) + 1;
	 //tmp get old data
	 tn.exp = tmp.ttl;
	 tn.data = k;
	 pn = find_node(rbt,&tn);
	 //if update, we had delete tn in rbt
	 //else update tn in rbt
	 if(pn != NULL)
		 delete_node(rbt,pn);
	}
 ret = insert_into_ttltree(rbt,k,mv->ttl);//ttl expired tree
 pthread_mutex_unlock(&rbt->lock);
 return 0;
}
////////////////////////////////////////////////////////////////////


int get_level(uchar *itor)
{
 int lvl = 0;
 uchar len = itor[0];
 while(len != 0)
	{
	 lvl ++;
	 itor += itor[0] + 1;
	 len = itor[0];
	 if(len > 63)
		return -1;
	}
 return lvl;
}


uchar *fill_all_records_in_msg(struct hlpc *h,struct hlpf *hf,int idx)
{
 int datalen,step = 0;
 uint16_t txtlen;
 uchar *tmp = NULL,*to = hf->to,*from = hf->from;
 struct fillmsg *fm = (struct fillmsg*)(hf->to);
 fm->type = htons(hf->type);
 fm->dclass = htons(CLASS_IN);
 fm->ttl = htonl(hf->ttl - global_now);
 if(hf->ttl == MAX_TTL + 1)
	fm->ttl = htonl(hf->ttl - 1);
 to = to + sizeof(struct fillmsg);
 if(hf->type == A)
	step= 4;
 if(hf->type == AAAA)
	step = 16;
 switch(hf->type) // no soa
	{
	 case A: //idx not used
	 case AAAA:
		 fm->len = htons(step);
		 memcpy(to,from,step);
		 to = to + step; //data
		 break;
	 case CNAME: case NS:
		 idx ++;
		 h[idx].name = from;
		 h[idx].off = to - hf->hdr;
		 h[idx].ref = -1;
		 h[idx].level = get_level(h[idx].name);
		 tmp = fill_name_in_msg(h,to,idx);
		 fm->len = htons(tmp - to);
		 to = tmp;
		 break;
	 case MX:
		memcpy(to,from,sizeof(uint16_t)); //ref
		from += sizeof(uint16_t); //2
		to += sizeof(uint16_t);
		idx ++;
		h[idx].name = from;
		h[idx].off = to - hf->hdr;
		h[idx].ref = -1;
		h[idx].level = get_level(h[idx].name);
		tmp = fill_name_in_msg(h,to,idx);
		fm->len = htons(tmp - to + sizeof(uint16_t));
		to = tmp;
		break;
	 case TXT:
		txtlen = *(uint16_t*)from;
		from += sizeof(uint16_t); //len
		memcpy(to,from,txtlen);
		fm->len = htons(txtlen);
		to += txtlen;
		break;
	 case SRV:
		memcpy(to,from,sizeof(uint16_t) * 3);
		from += sizeof(uint16_t) * 3;
		to = to + sizeof(uint16_t) * 3;
		idx ++;
		h[idx].name = from;
		h[idx].off = to - hf->hdr;
		h[idx].ref = -1;
		h[idx].level = get_level(h[idx].name);
		tmp = fill_name_in_msg(h,to,idx);
		fm->len = htons(tmp - to + sizeof(uint16_t) * 3);
		to = tmp;
		break;
	 default:
		 break;
	}
 return to;
}


//return the match length in the end of two strings.
//NOT include the end "."
int reverse_compare(uchar *from,int flen,uchar *to,int tolen)
{
 uchar fi,ti,rec = 0;
 int match = 0;
 flen -= 2; //1 for strlen + 1, 1 for array in c
 tolen -= 2;
 fi = from[flen];
 ti = to[tolen];
 while(flen && tolen)
	{
	 if(fi != ti)
		break;
	 rec ++;
	 if(fi == (rec - 1)) //not include len itself
		{
		 match ++;
		 rec = 0;
		}
	 fi = from[-- flen];
	 ti = to[-- tolen];
	}
 return match;
}


//imxg3.douban.com imxg3.douban.com.cdn20.com.
uchar *fill_name_in_msg(struct hlpc *h,uchar *to,int idx)
{
 int i,mm = 0,m = 0,len,fill = 0,ml = 0,jump = 0,off = 0;
 const ushort base = 0xc000;
 uchar *itor = h[idx].name,*dn = NULL;
 if(idx == 0)
	{
	 *(ushort*)to = htons(h[0].off + base);
	 to += sizeof(ushort);
	 return to;
	}
 len = strlen(h[idx].name);
 for(i = idx - 1;i >= 0;i --)
	{
	 m = reverse_compare(h[i].name,strlen(h[i].name) + 1,h[idx].name,len + 1);
	 if(m > h[i].mt)
		{
		 h[idx].mt = m;//max match
		 h[idx].ref = i;
		}
	}
 if(mm > len)
	return NULL;
 if(h[idx].mt >= 0)
	fill = h[idx].level - h[idx].mt;
 else
	fill = h[idx].level;
 for(i = 0;i < fill;i ++)
	{
	 memcpy(to,itor,itor[0] + 1);//len.label
	 to = to + itor[0] + 1;
	 itor = itor + itor[0] + 1;
	}
 len = 0;
 if(h[idx].ref >= 0)
	{
	 dn = h[h[idx].ref].name;
	 jump = h[h[idx].ref].level - h[idx].mt;
	 for(i = 0;i < jump;i ++)
		{
		 len += dn[0] + 1;
		 dn += dn[0] + 1;
		}
	 off = h[h[idx].ref].off + len;
	 *(ushort*)to = htons(off + base);
	 to += 2;
	}
 else
	{
	 to[0] = 0;//no compression
	 to ++;
	}
 return to;
}


//jump from author.c
uchar *fill_rrset_in_msg(struct hlpc *h,uchar *from,uchar *to,int n,uchar *hdr,uint16_t *ttloff)
{
 uchar type;
 type = from[0];
 int i,step = 0,ttloffidx;
 uint16_t txtlen = 0;
 struct hlpf hf;
 struct mvalue *mv = NULL;
 ttloffidx = ttloff[0]; //must be 0
 from ++;//type
 mv = (struct mvalue*)from;
 from = from + sizeof(struct mvalue);
 hf.hdr = hdr;
 hf.ttl = mv->ttl;
 hf.type = type;
 if(type == A)
	step = 4;
 if(type == AAAA)
	step = 16;
 switch(type) //7
	{
	 case A: case AAAA:
		for(i = 0;i < mv->num;i ++)
			{
			 to = fill_name_in_msg(h,to,n);
			 hf.from = from;
			 hf.to = to;
			 //jump type and dclass
			 //then we get ttl's position
			 //plus hdr we get it's offset
			 //only for A record
			 if(type == A)
				{
				 //ttloff[0] idx
				 //+1, jump it
				 ttloff[ttloff[0] + 1] = to + sizeof(uint16_t) + sizeof(uint16_t) - hdr;
				 ttloff[0] ++;
				}
			 to = fill_all_records_in_msg(h,&hf,n);
			 from += step;
			}
		return to;
		break;
	 case CNAME: // cname must has 1 record
		 to = fill_name_in_msg(h,to,n);
		 hf.from = from;
		 hf.to = to;
		 to = fill_all_records_in_msg(h,&hf,n);
		 return to;
		 break;
	 case NS:
		for(i = 0;i < mv->num;i ++)
			{
			 to = fill_name_in_msg(h,to,n);
			 hf.from = from;
			 hf.to = to;
			 to = fill_all_records_in_msg(h,&hf,n);
			 from += strlen(from) + 1;
			}
		return to;
		break;
	 case MX:
		for(i = 0;i < mv->num;i ++)
			{
			 to = fill_name_in_msg(h,to,n);
			 hf.from = from;
			 hf.to = to;
			 to = fill_all_records_in_msg(h,&hf,n);
			 from += strlen(from) + 1; //ref and name
			}
		return to;
		break;
	 case TXT:
		for(i = 0;i < mv->num;i ++)
			{
			 to = fill_name_in_msg(h,to,n);
			 hf.from = from;
			 hf.to = to;
			 to = fill_all_records_in_msg(h,&hf,n);
			 txtlen = *(uint16_t*)from;
			 from = from + txtlen + sizeof(uint16_t);
			}
		return to;
		break;
	 case SRV:
		for(i = 0;i < mv->num;i ++)
			{
			 to = fill_name_in_msg(h,to,n);
			 hf.from = from;
			 hf.to = to;
			 to = fill_all_records_in_msg(h,&hf,n);
			 from += sizeof(uint16_t) * 3; //pri wei port
			 from += strlen(from) + 1; //target
			}
		 return to;
		 break;
	 default:
		printf("not support or error in fill msg\n");
		break;
	}
 return NULL;
}


uchar *fill_header_in_msg(struct setheader *sh)
{
 uchar llen = 0;
 uchar *itor = sh->itor;
 dnsheader *hdr = (dnsheader*)(sh->itor);
 qdns *qd;
 int i,dlen;
 hdr->id = sh->id;
 hdr->flags = 0;
 hdr->flags = SET_QR_R(hdr->flags);
 hdr->flags = SET_RA(hdr->flags);
 //hdr->flags = SET_ERROR(hdr->flags,mf->ed);
 hdr->flags = htons(hdr->flags);
 hdr->qdcount = htons(1);
 hdr->ancount = htons(sh->an);
 hdr->nscount = htons(sh->ns);
 hdr->arcount = htons(0);
 itor += sizeof(dnsheader);
 dlen = strlen(sh->od) + 1;
 memcpy(itor,sh->od,dlen);
 itor = itor + dlen;
 qd = (qdns*)itor;
 qd->type = htons(sh->type);
 qd->dclass = htons(CLASS_IN);
 itor += sizeof(qdns);
 return itor;
}


int make_dns_msg_for_new(uchar *itor,ushort msgid,uchar *d,ushort type)
{
 uchar *buf = itor;
 int need = 0,i,len;
 dnsheader *hdr = NULL;
 qdns *qd = NULL;
 hdr = (dnsheader*)buf;
 hdr->id = msgid;
 hdr->flags = htons(0x0100); //rd
 hdr->qdcount = htons(1);
 hdr->ancount = hdr->nscount = hdr->arcount = htons(0);
 buf += sizeof(dnsheader);
 len = strlen(d) + 1;
 memcpy(buf,d,len + 1);
 buf += len;
 qd = (qdns*)buf;
 qd->type = htons(type);
 qd->dclass = htons(CLASS_IN); 
 buf = buf + 4;
 return buf - itor; //msg len
}


//a,ns,txt,cname,soa,srv,aaaa,mx
int fill_rrset_in_buffer(uchar *buffer,uchar *label,uchar *hdr,int lth,int type,struct hlpp *hlp)
{
 int mlen = 0;
 uint16_t len = lth;
 uchar nsc[512] = {0};
 struct srv *from,*to;
 switch(type)
	{
	 case A:
		mlen = 4;
		memcpy(buffer,label,4);
		break;
	 case NS:
		get_domain_from_msg(label,hdr,nsc);
		hlp->dmsidx ++;
		strcpy(hlp->dms + hlp->dmsidx * DMS_SIZE,nsc);
		mlen = strlen(nsc) + 1;
		memcpy(buffer,nsc,mlen);
		break;
	 case CNAME:
		get_domain_from_msg(label,hdr,nsc);
		hlp->dmsidx ++;	
		strcpy(hlp->dms + hlp->dmsidx * DMS_SIZE,nsc);
		mlen = strlen(nsc) + 1;
		memcpy(buffer,nsc,mlen);
		break;
	 case SOA: //do nothing
		mlen = 0;
		break;
	 case AAAA:
		mlen = 16;
		memcpy(buffer,label,16);
		break;
	 case MX:
		memcpy(buffer,label,2); //reference value
		label += 2; //16bits
		buffer += 2;
		get_domain_from_msg(label,hdr,nsc);
		hlp->dmsidx ++;
		strcpy(hlp->dms + hlp->dmsidx * DMS_SIZE,nsc);
		mlen = strlen(nsc) + 1;
		memcpy(buffer,nsc,mlen);
		mlen += 2;
		break;
	 case SRV:
		from = (struct srv*)label;
		to = (struct srv*)buffer;
		to->pri = from->pri; //net endian
		to->wei = from->wei;
		to->port = from->port;
		buffer += sizeof(uint16_t) * 3;
		label += sizeof(uint16_t) * 3;
		get_domain_from_msg(label,hdr,nsc);
		mlen = strlen(nsc) + 1;
		memcpy(buffer,nsc,mlen);
		mlen += sizeof(uint16_t) * 3;
		break;
	 case TXT: //the only case that lth used
		memcpy(buffer,&len,sizeof(uint16_t));//uint16_t
		buffer += sizeof(uint16_t);
		memcpy(buffer,label,lth);
		mlen = lth + sizeof(uint16_t);
		break;
	 default:
		return -1;
	}
 return mlen;
}


//-1 error
//1  tc
//0 normal
//2 retry
int check_an_msg(ushort flag,uchar *domain,int *bk)
{
 uint get = 0;
 flag = ntohs(flag);
 //printf("flag is 0x%x\n",flag);
 get = GET_QR(flag);
 if(get == QR_Q) //query
	{
	 printf("answer set Q sign\n");
	 return -1;
	}
 get = GET_OPCODE(flag); //ignore.
 get = GET_AA(flag); //ignore
 get = GET_TC(flag);
 if(get == 1)
	 return 1; //tc
 get = GET_RD(flag); //ignore
 get = GET_ERROR(flag);
 if((get != 0) && (get != NAME_ERROR)) //soa
	{
	 switch(get)
		{
		 case SERVER_FAIL:
			//printf("2server fail\n");
			break;
		 //case NAME_ERROR: SOA
			//*bk = 1;
			//printf("3name error\n");
			//break;
		 case FORMAT_ERROR:
			//*bk = 1;
			//printf("1format error\n");
			break;
		 case NOT_IMPL:
			//printf("4not implation\n");
			break;
		 case REFUSED:
			//printf("5server refused\n");
			break;
		}
	 return 2;
	}
 return 0;
}


int check_out_msg(ushort cid,uchar *buf,int len)
{
 dnsheader *hdr = (dnsheader*)buf;
 hdr->id = cid;
 hdr->flags = 0;
 hdr->flags = htons(SET_QR_R(hdr->flags));
 return 0;
}


int check_td(uchar *td)
{
 uchar type = td[0];
 uchar *itor = td + 1;
 uchar len = itor[0];
 if((type != A) && (type != NS) && (type != CNAME))
	return -1;
 while(len != 0)
	{
	 if(len > 50)
		return -1;
	 itor = itor + len + 1;
	 len = itor[0];
	}
 return 0;
}


//if ns is domain's child or child's child or...
//domain and ns are td format
//type.domain
//002,005,b,a,i,d,u,003,c,o,m
//003,n,s,'4',005,b,a,i,d,u,003,c,o,m
int is_glue(uchar *domain,uchar *ns)
{
 uchar d,n;
 int dlen,nlen;
 dlen = strlen(domain);
 nlen = strlen(ns);
 dlen --;
 nlen --;
 if(dlen >= nlen)
	return 0;
 d = domain[dlen];
 n = ns[nlen];
 while(d == n)
	{
	 dlen --;
	 nlen --;
	 if(dlen == 0)
		return 1;
	 d = domain[dlen];
	 n = ns[nlen];
	}
 return 0;
}


//First ensure the search name, if it has a cname, search the cname
//If we find it in fwd table, return the ip length, it's > 0
//Here we dont care the cname in fwd table, if somebody want to do this
//Add the main domain in fwd table
int pre_find(struct qoutinfo *qo,struct htable *fwd,struct htable *ht,uchar *ip)
{
 uchar td[512] = {0},*itor = NULL;
 int xlen = 0,dbg = 100;
 uchar buffer[2000] = {0};
 struct mvalue *mv = NULL;
 qo->qname = Q_DOMAIN; //default
 if(qo->hascname == 1)
	{
	 qo->qing = qo->qbuffer; //latest cname
	 memcpy(td + 1,qo->qbuffer,strlen(qo->qbuffer) + 1);
	}
 else
	{
	 memcpy(td,qo->td,qo->dlen + 1);
	 qo->qing = qo->td + 1;
	}
 td[0] = A; //forward ip
 xlen = htable_find(fwd,td,ip,1900,NULL); //100 for struct mvalue
 if(xlen > 0)
	{
	 ip = ip + xlen;
	 mv = (struct mvalue*)ip;
	 mv->num = 0; //tail 0
	 mv->ttl = 0;
	 mv->hits = 0;
	 mv->len = 0;
	 return xlen;
	}
 if(qo->td[0] == CNAME) //query cname
	return 0; //find nothing
 td[0] = CNAME;
 itor = buffer;
 while(1) //find cname
	{
	 xlen = find_record_with_ttl(ht,td,itor,2000,NULL);
	 if(xlen > 0)
		{//if domain has a cname, put it in qo->qbuffer
		 qo->qname = Q_CNAME;
		 qo->hascname = 1;
		 mv = (struct mvalue*)itor;
		 itor = itor + sizeof(struct mvalue);
		 memcpy(td + 1,itor,mv->len);
		 if(mv->len > (QBUFFER_SIZE - 1))
			return -1;
		 memcpy(qo->qbuffer,itor,mv->len);
		 qo->qing = qo->qbuffer;
		}
	 else
		break;
	 if((dbg --) == 0)
		 return -1;
	}
 return 0;
}


//format of buff
//struct mvaule
//ttloff
//msg
int transfer_record_to_msg(uchar *buff,uchar *key,uchar *msg,int msglen,uint16_t *ttloff)
{
 uint16_t segs = ttloff[0],i,*len = NULL,totallen = 0;
 uchar *val = NULL,*itor = NULL;
 struct mvalue *mv = NULL;
 totallen = msglen;
 totallen = totallen + segs * sizeof(uint16_t) + sizeof(struct mvalue);
 if(totallen > MAX_MSG_SIZE)
	return -1;
 itor = buff;
 mv = (struct mvalue*)itor;
 mv->seg = segs;
 mv->len = msglen; //not include len of ttloff and mvalue
 itor = itor + sizeof(struct mvalue); //jump mvalue
 memcpy(itor,ttloff + 1,sizeof(uint16_t) * segs); //copy ttloff
 itor = itor + sizeof(uint16_t) * segs; //jump ttloff
 memcpy(itor,msg,msglen); //copy msg
 //seg and len are useful
 //ttl and hits are empty
 //num is invalid
 return 0;
}


///format of segment
//struct mvalue
//off.off.off.off...[mvalue->seg]
//msg
//off point ttl now
//we jump ttl and rdlength
//then we get raw A record data
//copy data from ipmsg to ipbuffer
//then copy data from ipbuffer to ipmsg
int make_A_record_from_segment(uchar *ipmsg)
{
 int reallen = 0;
 uchar ipbuffer[400] = {0};
 uchar *ipto = NULL,*ipfrom = NULL;
 struct mvalue *mv = NULL;
 uint16_t off;
 int segs = 0,i;
 mv = (struct mvalue*)ipmsg;
 segs = mv->seg;
 ipto = ipbuffer;
 for(i = 0;i < segs;i ++)
	{
	 off = *(uint16_t*)(ipmsg + sizeof(struct mvalue) + i * sizeof(uint16_t));
	 ipfrom = ipmsg + off;
	 memcpy(ipto,ipfrom,4);
	 reallen += 4;
	 ipto += 4;
	}
 mv->len = reallen;
 ipfrom = ipbuffer;
 ipto = ipmsg + sizeof(struct mvalue);
 memcpy(ipto,ipfrom,reallen);
 return 0;
}


//we found some ns
//try to find their ip
int retrive_ip(uchar *itor,int num,uchar *ip,struct htable *ht,int *fq)
{
 struct mvalue *mi = NULL;
 int i,xlen,iplen = IP_DATA_LEN;
 int got = 0;
 uchar ipbuffer[400] = {0};
 *fq = 0;
 uchar nstd[512] = {0},*iitor = ip;
 for(i = 0;i < num;i ++)
	{
	 nstd[0] = A; //NS's A
	 xlen = strlen(itor) + 1;
	 memcpy(nstd + 1,itor,xlen);
	 itor = itor + xlen;
	 xlen = find_record_with_ttl(ht,nstd,ipbuffer,iplen - sizeof(struct mvalue),NULL);
	 if(xlen > 0)
		{
		 mi = (struct mvalue*)ipbuffer;
		 if(mi->seg > 0) //segment
			make_A_record_from_segment(ipbuffer);
		 //after make_A_xxxx
		 //ipbuffer changed and mi changed too
		 memcpy(iitor,ipbuffer,mi->len + sizeof(struct mvalue));
		 iitor = iitor + mi->len + sizeof(struct mvalue);
		 iplen = iplen - mi->len - sizeof(struct mvalue);
		 got ++;
		}
	 if(xlen < 0) //iplen is not enough
		{
		 *fq = i;
		 break;
		}
	}
 if(iitor != ip) //found some ip
	{
	 mi = (struct mvalue*)iitor;
	 mi->num = 0; //tail 0
	 mi->ttl = 0;
	 mi->hits = 0;
	 mi->len = 0;
	 return got;
	}
 return -1; //no ip
}


int fill_extra_addr(struct qoutinfo *qo,uchar *ip)
{
 const char *extra[] = {
 "8.8.8.8", //google dns
 "202.102.154.3", //public dns of shandong province
};
 int i,n;
 struct mvalue *mv = NULL;
 n = sizeof(extra) / sizeof(extra[0]);
 mv = (struct mvalue*)ip;
 ip = ip + sizeof(struct mvalue);
 mv->num = 0;
 mv->ttl = 0;
 mv->hits = 0;
 mv->len = 0;
 for(i = 0;i < n;i ++)
	{
	 if(make_bin_from_str(ip,extra[i]) == 0)
		{
		 mv->num ++;
		 mv->len += 4; //4 bytes
		 ip += 4; //4 bytes
		}
	}
 mv = (struct mvalue*)ip;
 mv->num = 0;
 mv->ttl = 0;
 mv->hits = 0;
 mv->len = 0;
 return 0;
}


//ht,type,domain,dlen
int find_addr(struct htable *fwd,struct htable *ht,struct qoutinfo *qo,uchar *ip)
{
 int ret,xlen = 0,dbg = 100,iplen = IP_DATA_LEN;
 int first_query,i;
 struct mvalue *mv = NULL;
 uchar td[512] = {0},buffer[IP_DATA_LEN] = {0},*itor = NULL,*glue = NULL;
 if(qo->qtimes > (MAX_TRY_TIMES - 3))
	{
	 fill_extra_addr(qo,ip);
	 return 0;
	}
 ret = pre_find(qo,fwd,ht,ip);
 if(ret > 0) //find fwd
	return 0;
 if(ret < 0) //error
	return ret;
 itor = td;
 //now we have domain or latest cname in qo->qing 
 //point to qo->td or qo->qbuffer
 memcpy(td + 1,qo->qing,strlen(qo->qing) + 1);
 td[0] = NS;
 while(1) //put ns in itor(buffer), put ns'a in iitor(ip)
	{
	 while(1)
		{
		 ret = find_record_with_ttl(ht,itor,buffer,IP_DATA_LEN,NULL); //ns do not
		 if(ret > 0)
			 break;
		 itor = itor + itor[1] + 1; //parent, assert itor[1] < 64
		 itor[0] = NS;
		 if(itor[1] == 0) //root
			return -1;
		 if((dbg --) == 0)//if mess buffer
			 return -1;
		}
	 mv = (struct mvalue*)buffer; //ns record in buffer
	 glue = itor; //data in td, real domain we get ns //key
	 itor = buffer + sizeof(struct mvalue); //data //value
	 ret = retrive_ip(itor,mv->num,ip,ht,&first_query);
	 if((ret > 0))
		{
		 if((ret < mv->num) && (qo->qns == 1))
			{
			 qo->qns = 0;
			 for(i = 0;i < first_query;i ++)
				{
				 xlen = strlen(itor) + 1;
				 itor = itor + xlen;
				}
			 xlen = strlen(itor) + 1;
			 memcpy(qo->qbuffer,itor,xlen);
			 qo->qing = qo->qbuffer;
			 memcpy(td + 1,qo->qbuffer,xlen);
			}
		 else
			return 0;
		}
	 if(is_glue(glue,buffer + sizeof(struct mvalue)) != 1) //domain and it's ns
		{
		 itor = buffer + sizeof(struct mvalue);
		 xlen = strlen(itor) + 1;//ns len
		 if(xlen > (QBUFFER_SIZE - 1))
			return -1;
		 memcpy(qo->qbuffer,itor,xlen);
		 qo->qing = qo->qbuffer;
		 memcpy(td + 1,qo->qbuffer,xlen);
		 itor = td; //itor point to key now
		}
	 else //qbuffer and qing need NO change
		itor = glue + glue[1] + 1; //glue[0] is type,glue[1] is label length
	 itor[0] = NS;
	 qo->qname = Q_NS;
	 if((dbg --) == 0)
		 return -1;
	}
 return 0;
}


//same as find from mem
//for debug
int check_qo(struct qoutinfo *qo)
{
 uchar type;
 if(qo == NULL)
	return 0;
 if(qo->hascname > 1)
	printf("qo error\n");
 if(qo->td == NULL)
	printf("qo error2\n");
 type = qo->td[0];
 return 0;
}


uchar* dbg_print_label(uchar *label,int visible)
{
 uchar i,len = (uchar)(*label);
 if(visible == 1)
 for(i = 1;i < len + 1;i ++)
	printf("%c",label[i]);
 return label + label[0] + 1;
}


uchar* dbg_print_domain(uchar *hdr,uchar *itor)
{
 uchar len;
 uchar *tmp = NULL;
 ushort offset;
 int i,debug = 100;
 len = itor[0]; 
 if(len == 0)
	{
	 printf("root\n");
	 return 0;
	}
 offset = htons((ushort)*(ushort*)itor);
 if(IS_PTR(offset))
	itor = hdr + GET_OFFSET(offset);
 while(len != 0 && debug --)
	{
 	 if(IS_PTR(offset))
		{
		 tmp = itor + 2;
		 itor = dbg_print_label(hdr + GET_OFFSET(offset),1);
		}
	 else
		 itor = dbg_print_label(itor,1);
	 printf(".");
	 len = itor[0];
	 offset = htons((ushort)*(ushort*)itor);
	}
 printf("\n");
 if(tmp == NULL)
	tmp = itor + 1;
 return tmp;
}

void dbg_print_ip(uchar *ip,enum rrtype type)
{
 int i;
 int hasbarr = 0;
 uint ipv4[4] = {0};
 for(i = 0;i < 4;i ++)
	ipv4[i] = *(uchar*)(ip + i);
 if(type == A)
	 printf("%u.%u.%u.%u\n",(unsigned short)ipv4[0],ipv4[1],ipv4[2],ipv4[3]);
 else if(type == AAAA){
 for(i = 0;i < 8;i ++)
	{
	 if(ip[i * 2] != 0)
		{
		 if(ip[i * 2] < 0x10)
			printf("0");
		 printf("%x",(uint)ip[i * 2]);
		}
	 if(ip[i * 2 + 1] < 0x10)
		printf("0");
	 printf("%x",(uint)ip[i * 2 + 1]);
	 if(i != 7)
		printf(":");
	}
 printf("\n");
}
 else
	printf("unknow type %d\n",type);
}


int dbg_print_td(uchar *td)
{
 uchar c = td[0];
 printf("%d,",c);
 dbg_print_domain(NULL,td + 1);
 return 0;
}
