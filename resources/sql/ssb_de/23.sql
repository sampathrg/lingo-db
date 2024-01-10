select d_year, p_brand, sum(cast(lo_revenue as int8))
from lineorder, date_dict_encoded_str, part_dict_encoded_str, supplier_dict_encoded_str
where lo_orderdate = d_datekey
and lo_partkey = p_partkey
and lo_suppkey = s_suppkey
and p_brand = 675 -- p_brand = 'MFGR#2239' (this seems wrong, see q22)
and s_region = 3 -- s_region = 'EUROPE'
group by d_year, p_brand
-- order by d_year, p_brand;
