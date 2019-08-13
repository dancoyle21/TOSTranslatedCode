#include "Compress.HC.h"

U0 ArcEntryGet(CArcCtrl *c)
{
  I64 i;
  CArcEntry *tmp,*tmp1;

  if (c->entry_used) {
    i=c->free_idx;

    c->entry_used=FALSE;
    c->cur_entry=c->next_entry;
    c->cur_bits_in_use=c->next_bits_in_use;
    if (c->next_bits_in_use<ARC_MAX_BITS) {
      c->next_entry = &c->compress[i++];
      if (i==c->free_limit) {
	c->next_bits_in_use++;
	c->free_limit=1<<c->next_bits_in_use;
      }
    } else {
      do if (++i==c->free_limit)
	  i=c->min_table_entry;
      while (c->hash[i]);
      tmp=&c->compress[i];
      c->next_entry=tmp;
      tmp1=(CArcEntry *)(&c->hash[tmp->basecode]);
      while (tmp1) {
	if (tmp1->next==tmp) {
	  tmp1->next=tmp->next;
	  break;
	} else
	  tmp1=tmp1->next;
      }
    }
    c->free_idx=i;
  }
}

I64 ArcDetermineCompressionType(U8 *src,I64 size)
{
  while (size--)
    if (*src++&0x80)
      return CT_8_BIT;
  return CT_7_BIT;
}

U0 ArcCompressBuf(CArcCtrl *c)
{//Use $LK,"CompressBuf",A="MN:CompressBuf"$() unless doing more than one buf.
  CArcEntry *tmp,*tmp1;
  I64 ch,basecode;
  U8 *src_ptr,*src_limit;

  src_ptr=c->src_buf+c->src_pos;
  src_limit=c->src_buf+c->src_size;

  if (c->saved_basecode==MAX_U32)
    basecode=*src_ptr++;
  else
    basecode=c->saved_basecode;

  while (src_ptr<src_limit && c->dst_pos+c->cur_bits_in_use<=c->dst_size) {
    ArcEntryGet(c);
ac_start:
    if (src_ptr>=src_limit) goto ac_done;
    ch=*src_ptr++;
    if (tmp=c->hash[basecode])
      do {
	if (tmp->ch==ch) {
	  basecode=tmp-&c->compress[0];
	  goto ac_start;
	}
      } while (tmp=tmp->next);

    BFieldOrU32(c->dst_buf,c->dst_pos,basecode);
    c->dst_pos+=c->cur_bits_in_use;

    c->entry_used=TRUE;
    tmp=c->cur_entry;
    tmp->basecode=basecode;
    tmp->ch=ch;
    tmp1=(CArcEntry *)(&c->hash[basecode]);
    tmp->next=tmp1->next;
    tmp1->next=tmp;

    basecode=ch;
  }
ac_done:
  c->saved_basecode=basecode;
  c->src_pos=src_ptr-c->src_buf;
}

Bool ArcFinishCompression(CArcCtrl *c)
{//Do closing touch on archivew ctrl struct.
  if (c->dst_pos+c->cur_bits_in_use<=c->dst_size) {
    BFieldOrU32(c->dst_buf,c->dst_pos,c->saved_basecode);
    c->dst_pos+=c->next_bits_in_use;
    return TRUE;
  } else
    return FALSE;
}

U0 ArcExpandBuf(CArcCtrl *c)
{//Use $LK,"ExpandBuf",A="MN:ExpandBuf"$() unless you know what you're doing.
  U8 *dst_ptr,*dst_limit;
  I64 basecode,lastcode,code;
  CArcEntry *tmp,*tmp1;

  dst_ptr=c->dst_buf+c->dst_pos;
  dst_limit=c->dst_buf+c->dst_size;

  while (dst_ptr<dst_limit && c->stk_ptr!=c->stk_base)
    *dst_ptr++ = * -- c->stk_ptr;

  if (c->stk_ptr==c->stk_base && dst_ptr<dst_limit) {
    if (c->saved_basecode==MAX_U32) {
      lastcode=BFieldExtU32(c->src_buf,c->src_pos,
	    c->next_bits_in_use);
      c->src_pos+=c->next_bits_in_use;
      *dst_ptr++=lastcode;
      ArcEntryGet(c);
      c->last_ch=lastcode;
    } else
      lastcode=c->saved_basecode;
    while (dst_ptr<dst_limit && c->src_pos+c->next_bits_in_use<=c->src_size) {
      basecode=BFieldExtU32(c->src_buf,c->src_pos,
	    c->next_bits_in_use);
      c->src_pos+=c->next_bits_in_use;
      if (c->cur_entry==&c->compress[basecode]) {
	*c->stk_ptr++=c->last_ch;
	code=lastcode;
      } else
	code=basecode;
      while (code>=c->min_table_entry) {
	*c->stk_ptr++=c->compress[code].ch;
	code=c->compress[code].basecode;
      }
      *c->stk_ptr++=code;
      c->last_ch=code;

      c->entry_used=TRUE;
      tmp=c->cur_entry;
      tmp->basecode=lastcode;
      tmp->ch=c->last_ch;
      tmp1=(CArcEntry*)(&c->hash[lastcode]);
      tmp->next=tmp1->next;
      tmp1->next=tmp;

      ArcEntryGet(c);
      while (dst_ptr<dst_limit && c->stk_ptr!=c->stk_base)
	*dst_ptr++ = * -- c->stk_ptr;
      lastcode=basecode;
    }
    c->saved_basecode=lastcode;
  }
  c->dst_pos=dst_ptr-c->dst_buf;
}

CArcCtrl *ArcCtrlNew(Bool expand,I64 compression_type=CT_8_BIT)
{//MAlloc archive ctrl struct.
  CArcCtrl *c;
  c=CAlloc(sizeof(CArcCtrl));
  if (expand) {
    c->stk_base=MAlloc(1<<ARC_MAX_BITS);
    c->stk_ptr=c->stk_base;
  }
  if (compression_type==CT_7_BIT)
    c->min_bits=7;
  else
    c->min_bits=8;
  c->min_table_entry=1<<c->min_bits;
  c->free_idx=c->min_table_entry;
  c->next_bits_in_use=c->min_bits+1;
  c->free_limit=1<<c->next_bits_in_use;
  c->saved_basecode=MAX_U32;
  c->entry_used=TRUE;
  ArcEntryGet(c);
  c->entry_used=TRUE;
  return c;
}

U0 ArcCtrlDel(CArcCtrl *c)
{//Free archive ctrl struct.
  Free(c->stk_base);
  Free(c);
}

U8 *ExpandBuf(CArcCompress *arc,CTask *mem_task=NULL)
{//See $LK,"::/Demo/Dsk/SerializeTree.HC"$.
  CArcCtrl *c;
  U8 *res;

  if (!(CT_NONE<=arc->compression_type<=CT_8_BIT) ||
	arc->expanded_size>MEM_MAPPED_SPACE)
    throw('Compress');

  res=MAlloc(arc->expanded_size+1,mem_task);
  res[arc->expanded_size]=0; //terminate
  switch (arc->compression_type) {
    case CT_NONE:
      MemCpy(res,&arc->body,arc->expanded_size);
      break;
    case CT_7_BIT:
    case CT_8_BIT:
      c=ArcCtrlNew(TRUE,arc->compression_type);
      c->src_size=arc->compressed_size<<3;
      c->src_pos=sizeof(CArcCompress)<<3;
      c->src_buf=(U8*)arc;
      c->dst_size=arc->expanded_size;
      c->dst_buf=res;
      c->dst_pos=0;
      ArcExpandBuf(c);
      ArcCtrlDel(c);
      break;
  }
  return res;
}

CArcCompress *CompressBuf(U8 *src,I64 size,CTask *mem_task=NULL)
{//See $LK,"::/Demo/Dsk/SerializeTree.HC"$.
  CArcCompress *arc;
  I64 size_out,compression_type=ArcDetermineCompressionType(src,size);
  CArcCtrl *c=ArcCtrlNew(FALSE,compression_type);
  c->src_size=size;
  c->src_buf=src;
  c->dst_size=(size+sizeof(CArcCompress))<<3;
  c->dst_buf=CAlloc(c->dst_size>>3);
  c->dst_pos=sizeof(CArcCompress)<<3;
  ArcCompressBuf(c);
  if (ArcFinishCompression(c) && c->src_pos==c->src_size) {
    size_out=(c->dst_pos+7)>>3;
    arc=MAlloc(size_out,mem_task);
    MemCpy(arc,c->dst_buf,size_out);
    arc->compression_type=compression_type;
    arc->compressed_size=size_out;
  } else {
    arc=MAlloc(size+sizeof(CArcCompress),mem_task);
    MemCpy(&arc->body,src,size);
    arc->compression_type=CT_NONE;
    arc->compressed_size=size+sizeof(CArcCompress);
  }
  arc->expanded_size=size;
  Free(c->dst_buf);
  ArcCtrlDel(c);
  return arc;
}