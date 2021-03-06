#include "ngx_http_secure_token_m3u8.h"

static u_char encryption_key_tag[] = "EXT-X-KEY";
static u_char uri_attr_name[] = "URI";

enum {
	STATE_INITIAL,
	STATE_TAG_NAME,
	STATE_ATTR_NAME,
	STATE_ATTR_VALUE,
	STATE_ATTR_QUOTED_VALUE,
	STATE_ATTR_QUOTED_VALUE_WITH_QUERY,
	STATE_ATTR_WAIT_DELIM,
	STATE_WAIT_NEWLINE,
	STATE_URL,
	STATE_URL_WITH_QUERY,
};

// The example below shows the trasitions between the different states (numbers represent the state value):
// #EXT-X-KEY:METHOD=AES-128,URI="encryption.key"
// 1         2      36      2   34              6

ngx_chain_t** 
ngx_http_secure_token_m3u8_processor(
	ngx_http_secure_token_processor_conf_t* conf,
	void* params,
	ngx_buf_t *in, 
	ngx_http_secure_token_ctx_t* root_ctx,
	ngx_http_secure_token_m3u8_ctx_t* ctx, 
	ngx_pool_t* pool, 
	ngx_chain_t** out)
{
	u_char* last_sent;
	u_char* cur_pos;
	u_char* buffer_end;
	u_char ch;

	last_sent = in->pos;
	buffer_end = in->last;
	for (cur_pos = in->pos; cur_pos < buffer_end; cur_pos++)
	{
		ch = *cur_pos;

		switch (ctx->state)
		{
		case STATE_INITIAL:
			if (ch == '#')
			{
				ctx->state = STATE_TAG_NAME;
				ctx->tag_name_len = 0;
			}
			else if (!isspace(ch))
			{
				if (conf->tokenize_segments)
				{
					ctx->state = STATE_URL;
					ctx->last_url_char = 0;
				}
				else
				{
					ctx->state = STATE_WAIT_NEWLINE;
				}
			}
			break;

		case STATE_TAG_NAME:
			if (ch == ':')
			{
				ctx->state = STATE_ATTR_NAME;
				ctx->attr_name_len = 0;
			}
			else if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			else if (ctx->tag_name_len < M3U8_MAX_TAG_NAME_LEN)
			{
				ctx->tag_name[ctx->tag_name_len] = ch;
				ctx->tag_name_len++;
			}
			break;

		case STATE_ATTR_NAME:
			if (ch == '=')
			{
				ctx->state = STATE_ATTR_VALUE;
			}
			else if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			else if (ctx->attr_name_len < M3U8_MAX_ATTR_NAME_LEN)
			{
				ctx->attr_name[ctx->attr_name_len] = ch;
				ctx->attr_name_len++;
			}
			break;

		case STATE_ATTR_VALUE:
			if (ch == '"')
			{
				ctx->state = STATE_ATTR_QUOTED_VALUE;
				ctx->last_url_char = 0;
			}
			else if (ch == ',')
			{
				ctx->state = STATE_ATTR_NAME;
				ctx->attr_name_len = 0;
			}
			else if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			else
			{
				// dont care about unquoted attribute values
				ctx->state = STATE_ATTR_WAIT_DELIM;
			}
			break;

		case STATE_ATTR_QUOTED_VALUE:
		case STATE_ATTR_QUOTED_VALUE_WITH_QUERY:
			if (ch == '"')
			{
				if (ctx->tag_name_len == sizeof(encryption_key_tag) - 1 &&
					ngx_memcmp(ctx->tag_name, encryption_key_tag, sizeof(encryption_key_tag) - 1) == 0 &&
					ctx->attr_name_len == sizeof(uri_attr_name) - 1 &&
					ngx_memcmp(ctx->attr_name, uri_attr_name, sizeof(uri_attr_name) - 1) == 0)
				{
					out = ngx_http_secure_token_add_token(
						root_ctx, pool, &last_sent, cur_pos, ctx->state == STATE_ATTR_QUOTED_VALUE_WITH_QUERY, ctx->last_url_char, out);
					if (out == NULL)
					{
						return NULL;
					}
				}

				ctx->state = STATE_ATTR_WAIT_DELIM;
			}
			else if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			else
			{
				ctx->last_url_char = ch;
				if (ch == '?')
				{
					ctx->state = STATE_ATTR_QUOTED_VALUE_WITH_QUERY;
				}
			}
			break;

		case STATE_ATTR_WAIT_DELIM:
			if (ch == ',')
			{
				ctx->state = STATE_ATTR_NAME;
				ctx->attr_name_len = 0;
			}
			else if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			break;

		case STATE_WAIT_NEWLINE:
			if (ch == '\n')
			{
				ctx->state = STATE_INITIAL;
			}
			break;

		case STATE_URL:
		case STATE_URL_WITH_QUERY:
			if (!isspace(ch))
			{
				ctx->last_url_char = ch;
				if (ch == '?')
				{
					ctx->state = STATE_URL_WITH_QUERY;
				}
				break;
			}

			out = ngx_http_secure_token_add_token(
				root_ctx, pool, &last_sent, cur_pos, ctx->state == STATE_URL_WITH_QUERY, ctx->last_url_char, out);
			if (out == NULL)
			{
				return NULL;
			}

			ctx->state = (ch == '\n' ? STATE_INITIAL : STATE_WAIT_NEWLINE);
			break;
		}
	}

	if (cur_pos > last_sent)
	{
		out = ngx_http_secure_token_add_to_chain(pool, out, last_sent, cur_pos, 1, 0);
		if (out == NULL)
		{
			return NULL;
		}
	}

	return out;
}
