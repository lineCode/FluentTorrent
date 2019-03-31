#include "stdafx.h"


#pragma comment(lib, "windowsapp")






int CALLBACK BrowseCallbackProc(
	HWND hwnd,
	UINT uMsg,
	LPARAM,
	LPARAM lpData
)
{
	if (uMsg == BFFM_INITIALIZED)
	{
		ITEMIDLIST* sel = (ITEMIDLIST*)lpData;
		if (!sel)
			return 0;
		SendMessage(hwnd, BFFM_SETSELECTION, false, (LPARAM)sel);
	}
	return 0;
}

#define USE_NAVIGATIONVIEW


#ifdef USE_NAVIGATIONVIEW
#define TopView NavigationView
std::wstring mvx = L"mv";
#else
#define TopView StackPanel
std::wstring mvx = L"mv1";
#endif


bool BrowseFolder(HWND hh, const TCHAR* tit, const TCHAR* root, const TCHAR* sel, TCHAR* rv)
{
	IShellFolder* sf = 0;
	SHGetDesktopFolder(&sf);
	if (!sf)
		return false;

	BROWSEINFO bi = { 0 };
	bi.hwndOwner = hh;
	if (root && _tcslen(root))
	{
		PIDLIST_RELATIVE pd = 0;
		wchar_t dn[1000] = { 0 };
		wcscpy_s(dn, 1000, root);
		sf->ParseDisplayName(hh, 0, dn, 0, &pd, 0);
		bi.pidlRoot = pd;
	}
	bi.lpszTitle = tit;
	bi.ulFlags = BIF_EDITBOX | BIF_NEWDIALOGSTYLE;
	bi.lpfn = BrowseCallbackProc;
	if (sel && _tcslen(sel))
	{
		PIDLIST_RELATIVE pd = 0;
		wchar_t dn[1000] = { 0 };
		wcscpy_s(dn, 1000, sel);
		sf->ParseDisplayName(hh, 0, dn, 0, &pd, 0);
		bi.lParam = (LPARAM)pd;
	}

	PIDLIST_ABSOLUTE pd = SHBrowseForFolder(&bi);
	if (!pd)
		return false;

	SHGetPathFromIDList(pd, rv);
	CoTaskMemFree(pd);
	return true;
}

#define UWPLIB_CUSTOMONLY
#include ".\\uwplib\\uwplib.hpp"
#include "sqlite3.h"




using namespace winrt::Windows::UI;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

using namespace std;
#include ".\\mt\\rw.hpp"

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/magnet_uri.hpp"

const wchar_t* mutn = L"Global\\FluentTorrent{2392B4A1-02B0-45D4-BFDB-0D6552496844}";



HWND MainWindow = 0;
HINSTANCE hAppInstance = 0;
HICON hIcon1 = 0;
const wchar_t* ttitle = L"Fluent Torrent";
using namespace std;
wstring LVSelected;
shared_ptr<lt::session> t_session;
UINT umsg = 0;


#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
using clk = std::chrono::steady_clock;


string hs(const lt::sha1_hash& hh)
{
	auto sz = hh.size();
	vector<char> d(sz*2 + 1);
	for (int i = 0; i < sz; i++)
	{
		char r[5] = { 0 };
		sprintf_s(r, 5, "%02X", hh[i]);
		d[i * 2] = r[0];
		d[(i * 2) + 1] = r[1];
	}
	return d.data();
}

HWND hX = 0;
UWPLIB::UWPCONTROL* c = 0;

#include "func.hpp"
#include <queue>

struct TREQUEST
{
	int Type = 0; //  1 Add Magnet, 2 Pause/Resume, 3 Resume, 4 Delete, 5 Add File, 6 delete + torrent
	string h;
	ystring t;
	unsigned long long row = 0;
};
tlock<queue<TREQUEST>> reqs;
bool End = false;
bool EndT1 = false;
bool EndT2 = false;


ystring Setting(const char* k, const char* def = "",bool W = false)
{

	if (W)
	{
		sqlite::query q(sql->h(), "DELETE FROM SETTINGS WHERE NAME = ?");
		q.BindText(1, k,(int) strlen(k));
		q.R();
		sqlite::query q2(sql->h(), "INSERT INTO SETTINGS (NAME,VALUE) VALUES (?,?)");
		q2.BindText(1, k, (int)strlen(k));
		q2.BindText(2, def, (int)strlen(def));
		q2.R();
	}

	sqlite::query q(sql->h(), "SELECT * FROM SETTINGS WHERE NAME = ?");
	q.BindText(1, k, (int)strlen(k));
	map<string, string> row;
	if (!q.NextRow(row))
		return def;
	return row["VALUE"];
}


tlock<vector<lt::torrent_handle>> th;
void BitThread()
{
	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::alert_mask,
		lt::alert::error_notification
		| lt::alert::storage_notification
		| lt::alert::status_notification
		| lt::alert::tracker_notification
		| lt::alert::connect_notification
	);

	int PN = _wtoi(Setting("TCPPORT", "7008").c_str());
	if (PN > 0)
	{
		ystring y;
		y.Format(L"0.0.0.0:%u", PN);
		pack.set_str(lt::settings_pack::listen_interfaces, y.a_str());
	}

	int DL = _wtoi(Setting("DOWNLOADLIMIT", "0").c_str());
	if (DL > 0) // In KB
	{
		pack.set_int(lt::settings_pack::download_rate_limit, DL * 1024);
	}

	int UL = _wtoi(Setting("UPLOADLIMIT", "0").c_str());
	if (UL > 0) // In KB
	{
		pack.set_int(lt::settings_pack::upload_rate_limit, UL*1024);
	}

//	download_rate_limit

	pack.set_str(lt::settings_pack::user_agent, lt::generate_fingerprint("FT", 1));
	lt::session ses(pack);
	ses.add_extension(&libtorrent::create_ut_metadata_plugin);
	ses.add_extension(&libtorrent::create_ut_pex_plugin);
	ses.add_extension(&libtorrent::create_smart_ban_plugin);
	auto last_save_resume = GetTickCount();
	// Load all torrents from database

	sqlite::sqlite sqlx("config.db");
	sqlite::query q(sqlx.h(), "SELECT * FROM TORRENTS");
	map<string, string> row;
	while (q.NextRow(row))
	{
		lt::add_torrent_params atp;
		if (strlen(row["RD"].c_str()))
		{
			XML3::BXML out;
			XML3::Base64ToChar(row["RD"].c_str(), row["RD"].length(), out);
			vector<char> oout(out.size());
			memcpy(oout.data(), out.operator char *(), oout.size());
			atp = lt::read_resume_data(oout);
			if (strlen(row["FILE"].c_str()))
				atp.ti = std::make_shared<lt::torrent_info>(row["FILE"]);
		}
		else
		{

			if (strlen(row["MAGNET"].c_str()))
				atp = lt::parse_magnet_uri(row["MAGNET"].c_str());
			else
				if (strlen(row["FILE"].c_str()))
					atp.ti = std::make_shared<lt::torrent_info>(row["FILE"]);
				else
					continue;
		}

		atp.userdata = (void*)atoi(row["ID"].c_str());
		atp.save_path = Setting("TORRENTDIR", ".\\TORRENTS");
		ses.async_add_torrent(std::move(atp));
	}


	vector<wstring> FilesToDeleteNextDeletion;
	for (;;)
	{
		if (End)
			break;

		// Also the  requests
		bool WasR = false;
		reqs.writelock([&](queue<TREQUEST>& r)
		{
			while (!r.empty())
			{
				WasR = true;
				auto& rr = r.front();

				if (rr.Type == 1) // Add Magnet
				{
					lt::add_torrent_params atp = lt::parse_magnet_uri(rr.t.a_str());
					atp.userdata = (void*)rr.row;
					atp.save_path = Setting("TORRENTDIR", ".\\TORRENTS");
					ses.async_add_torrent(std::move(atp));
				}

				if (rr.Type == 5) // Add File
				{
					lt::add_torrent_params atp;
					atp.ti = std::make_shared<lt::torrent_info>((string)rr.t);
					atp.userdata = (void*)rr.row;
					atp.save_path = Setting("TORRENTDIR", ".\\TORRENTS");
					ses.async_add_torrent(std::move(atp));
				}

				if (rr.Type == 2) // Pause
				{
					auto v = ses.get_torrents();
					for (auto& vv : v)
					{
						if (hs(vv.info_hash()) == rr.h)
						{
							vv.pause();
						}
					}
				}
				if (rr.Type == 3) // Resume
				{
					auto v = ses.get_torrents();
					for (auto& vv : v)
					{
						if (hs(vv.info_hash()) == rr.h)
						{
							vv.resume();
						}
					}
				}
				if (rr.Type == 4) // Delete
				{
					auto v = ses.get_torrents();
					for (auto& vv : v)
					{
						if (hs(vv.info_hash()) == rr.h)
						{
							ses.remove_torrent(vv);
							break;
						}
					}
				}
				if (rr.Type == 6) // Delete + files
				{
					auto v = ses.get_torrents();
					for (auto& vv : v)
					{
						if (hs(vv.info_hash()) == rr.h)
						{
							// Files to Delete 
							vector<wstring> fils;
							size_t n = vv.torrent_file()->files().num_files();
							for (int iif = 0; iif < n; iif++)
							{
								ystring np = Setting("TORRENTDIR", ".\\TORRENTS").c_str();
								np += L"\\";
								ystring np2 = vv.torrent_file()->files().file_path(iif).c_str();
								np += np2;
								fils.push_back(np);
							}
							FilesToDeleteNextDeletion = fils;
							ses.remove_torrent(vv);
							break;
						}
					}
				}
				r.pop();
			}
		});

		if (WasR)
			continue;

		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);


		// The libtorrent alerts
		for (lt::alert const* a : alerts)
		{

			if (auto rd = lt::alert_cast<lt::save_resume_data_alert>(a))
			{
				lt::torrent_handle h = rd->handle;
				if (h.is_valid())
				{
					lt::torrent_status st = h.status(lt::torrent_handle::query_save_path | lt::torrent_handle::query_name);
					stringstream out;
					lt::bencode(std::ostream_iterator<char>(out), *rd->resume_data);

					sqlite::query q(sqlx.h(), "UPDATE TORRENTS SET RD = ? WHERE HASH = ?");
					string hh = hs(h.info_hash());
					string b = XML3::Char2Base64((const char*)out.str().data(), out.str().size());

					q.BindText(1, b.c_str(), b.length());
					q.BindText(2, hh.c_str(), hh.length());
					q.R();
				}

			}

			if (auto at = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				th->push_back(at->handle);
				sqlite::query q(sqlx.h(), "UPDATE TORRENTS SET HASH = ? WHERE ID = ?");
				string hh = hs(at->handle.info_hash());
				q.BindText(1, hh.c_str(), hh.length());
				char t[10] = { 0 };
				sprintf_s(t, 10, "%llu", (unsigned long long)at->params.userdata);
				q.BindText(2, t, strlen(t));
				q.R();
				SendMessage(MainWindow, WM_USER + 551, (WPARAM)0, (LPARAM)&at->handle);
			}

			if (auto at = lt::alert_cast<lt::torrent_removed_alert>(a))
			{
				string hh = hs(at->info_hash);
				SendMessage(MainWindow, WM_USER + 553, (WPARAM)&hh, (LPARAM)&at->handle);

				for (auto& f : FilesToDeleteNextDeletion)
					DeleteFile(f.c_str());
				FilesToDeleteNextDeletion.clear();



				th.writelock([&](vector<lt::torrent_handle>& thh) {
					thh = ses.get_torrents();
				});
			}


			// if we receive the finished alert or an error, we're done
			if (auto st = lt::alert_cast<lt::torrent_finished_alert>(a))
			{
				st->handle.save_resume_data();
				auto sta = st->handle.status();

				bool InScan = false;

				// Already scanned?
				bool NoScan = false;
				auto ha = hs(sta.info_hash);
				sqlite::query q(sql->h(), "SELECT * FROM TORRENTS WHERE HASH = ?");
				q.BindText(1, ha.c_str(), strlen(ha.c_str()));
				map<string, string> row;
				if (q.NextRow(row))
				{
					if (row["SCANNED"] == string("1"))
						NoScan = true;
				}

				if (!NoScan)
				{
					void AVScan(lt::torrent_handle t);
					if (Setting("SCANFINISHED", "0") == ystring("1"))
					{
						InScan = true;
						AVScan(st->handle);
					}
				}
				if (!InScan)
					SendMessage(MainWindow, WM_USER + 552, 1, (LPARAM)&sta);
			}
			if (lt::alert_cast<lt::torrent_error_alert>(a))
			{
				End = true;
				break;
			}

			if (auto st = lt::alert_cast<lt::state_update_alert>(a)) {

				for (size_t i = 0; i < st->status.size(); i++)
				{
					auto& sta = st->status[i];
					SendMessage(MainWindow, WM_USER + 552, 0, (LPARAM)&sta);
				}
			}
		}
		Sleep(250);
		ses.post_torrent_updates();

		// save resume data once every 30 seconds
		if ((GetTickCount() - last_save_resume) > 30000) {
			th.readlock([](const vector<lt::torrent_handle>& v) {
				for (auto& t : v)
				{
					if (t.is_valid())
					{
						t.save_resume_data();
					}
				}
			});
			last_save_resume = GetTickCount();
		}
	}


	th.readlock([](const vector<lt::torrent_handle>& v) {
		for (auto& t : v)
		{
			if (t.is_valid())
			{
				t.save_resume_data();
				lt::torrent_status ts = t.status();
				SendMessage(MainWindow, WM_USER + 552, 0, (LPARAM)&ts);
			}
		}
	});

	EndT1 = true;
}

void UpdateListView(const lt::torrent_handle* e,WPARAM Rem = 0)
{
	// Update the ListView
	TopView nv = c->ins.as<TopView>();
	try
	{
		auto lv = nv.FindName(L"torrlist").as<ListView>();
		auto its = lv.Items();
//		auto i = e->id();
		auto h = hs(e->info_hash());

		if (Rem)
		{
			h = *(string*)Rem;
			for (uint32_t i = 0; i < its.Size(); i++)
			{
				wstring n = its.GetAt(i).as<StackPanel>().Name().c_str();
				string wi = ystring(n.c_str() + 10);
				if (wi == h)
				{
					its.RemoveAt(i);
					sqlite::query q(sql->h(),"DELETE FROM TORRENTS WHERE HASH = ?");
					q.BindText(1, h.c_str(), h.length());
					q.R();

					// Hide the pivot
					StackPanel pvs = nv.FindName(L"pivotstack").as<StackPanel>();
					Pivot pitt = pvs.Children().GetAt(0).as<Pivot>();
					pitt.Visibility(Visibility::Collapsed);


					break;
				}
			}
			return;
		}

		auto tinfo = e->torrent_file();
		ystring sp;
		sp = ystring().Format(
			LR"(
		<StackPanel Name="StackPanel%S" Tag="%S"  xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Orientation="Horizontal">
			<StackPanel>
				<TextBlock FontWeight="Bold" Name="Text%S" Text="" MaxWidth="380" TextWrapping="NoWrap" />
				<ProgressBar Margin="0,0,0,0" Name="Prg%S" Value="0" Maximum="100" Width="400" IsIndeterminate="true"/>
			</StackPanel>
			<TextBlock Margin="20,0,0,0" Name="KB%S" Text="Please wait..." MaxWidth="150" Width="150" />
			<TextBlock Margin="20,0,0,0" Name="RA%S" Text="Please wait..." MaxWidth="100" Width="100" />
			<TextBlock Margin="20,0,0,0" Name="PI%S" Text="" MaxWidth="40" Width="40" />
		</StackPanel>)", h.c_str(), h.c_str(), h.c_str(), h.c_str(), h.c_str(), h.c_str(), h.c_str());
		using namespace winrt::Windows::UI::Xaml::Markup;
		auto ins = XamlReader::Load(sp.c_str());
		its.Append(ins);
	}
	catch (...)
	{
	}
}


void DeleteTorrent(const char *hss)
{
	string hash = hss;
	// Delete this torrent
	Setting("HASHTODELETE", hash.c_str(), true);
	th.readlock([&](const vector<lt::torrent_handle>& v) {

		for (auto& vv : v)
		{
			if (hs(vv.info_hash()) == hash)
			{

				if (vv.status().state == lt::torrent_status::seeding)
				{
					reqs.writelock([&](queue<TREQUEST>& r)
					{
						TREQUEST rr;
						rr.Type = 4;
						rr.h = Setting("HASHTODELETE", "");
						Setting("HASHTODELETE", "", true);
						r.push(rr);
					});
				}
				else
				{
					// This is the one
					TopView sp = c->ins.as<TopView>();
					auto dlg = sp.FindName(L"DeleteTorrentDlg").as<ContentDialog>();
					dlg.FindName(L"TorrToDelete").as<TextBlock>().Text(ystring(vv.status().name.c_str()));
					auto apo = dlg.ShowAsync();

					auto b3 = dlg.FindName(L"TorrDelButton3").as<Button>();
					b3.Click([](const IInspectable& ins, const RoutedEventArgs& r)
					{
						TopView sp = c->ins.as<TopView>();
						auto dlg = sp.FindName(L"DeleteTorrentDlg").as<ContentDialog>();
						dlg.Hide();
					});

					auto b1 = dlg.FindName(L"TorrDelButton1").as<Button>();
					b1.Click([](const IInspectable& ins, const RoutedEventArgs& r)
					{
						TopView sp = c->ins.as<TopView>();
						auto dlg = sp.FindName(L"DeleteTorrentDlg").as<ContentDialog>();
						dlg.Hide();

						reqs.writelock([&](queue<TREQUEST>& r)
						{
							TREQUEST rr;
							rr.Type = 4;
							rr.h = Setting("HASHTODELETE", "");
							Setting("HASHTODELETE", "", true);
							r.push(rr);
						});
					});

					auto b11 = dlg.FindName(L"TorrDelButton2").as<Button>();
					b11.Click([](const IInspectable& ins, const RoutedEventArgs& r)
					{
					TopView sp = c->ins.as<TopView>();
						auto dlg = sp.FindName(L"DeleteTorrentDlg").as<ContentDialog>();
						dlg.Hide();

						reqs.writelock([&](queue<TREQUEST>& r)
						{
							TREQUEST rr;
							rr.Type = 6;
							rr.h = Setting("HASHTODELETE", "");
							Setting("HASHTODELETE", "", true);
							r.push(rr);
						});
					});


				}

				break;
			}
		}
	});
}

tlock<queue<tuple<string,vector<wstring>,string>>> scanqueue;
void AVThread()
{
	HAMSICONTEXT h = 0;
	if (FAILED(AmsiInitialize(ttitle, &h)))
		return;
	for (;;)
	{
		if (End)
			break;
		Sleep(1000);


		vector<wstring> files;
		string hash;
		string tn;
		scanqueue.writelock([&](queue<tuple<string, vector<wstring>,string>>& vv) {
			if (vv.empty())
				return;
			auto& m = vv.front();
			tn = get<2>(m);
			files = get<1>(m);
			hash = get<0>(m);
			vv.pop();
		});

		if (files.empty())
			continue;


		ystring y;
		y.Format(L"Scanning %s...",ystring(tn.c_str()).c_str());
		SendMessage(MainWindow, WM_USER + 554, 0, (LPARAM)y.c_str());

		bool FoundMalware = false;
		for (size_t i = 0; i < files.size(); i++)
		{
			MMFILE m(files[i].c_str());
			if (m.size() == 0)
				continue;
			AMSI_RESULT ar;
			y.Format(L"Scanning [%s] %s...", ystring(tn.c_str()).c_str(),files[i].c_str());
			SendMessage(MainWindow, WM_USER + 554, 0, (LPARAM)y.c_str());
			if (FAILED(AmsiScanBuffer(h, (PVOID)m.operator const char *(), m.size(), files[i].c_str(), 0, &ar)))
				continue;
			if (AmsiResultIsMalware(ar))
			{
				y.Format(L"Found malware\r\n %s", files[i].c_str());
				SendMessage(MainWindow, WM_USER + 554,(WPARAM)y.c_str(),0);
				FoundMalware = true;
			}
		}

		if (FoundMalware)
		{
		//	MessageBox(MainWindow, L"Torrent contains malware", ttitle, MB_OK);
		}
		else
		{
			sqlite::sqlite sqlx("config.db");
			sqlite::query q(sqlx.h(), "UPDATE TORRENTS SET SCANNED = 1 WHERE HASH = ?");
			q.BindText(1, hash.c_str(), hash.length());
			q.R();
		}
		y.Format(L"Scanning %s finished.", ystring(tn.c_str()).c_str());
		SendMessage(MainWindow, WM_USER + 554, 0, (LPARAM)y.c_str());

		
	}
	EndT2 = true;
	AmsiUninitialize(h);
}


void AVScan(lt::torrent_handle t)
{

	vector<wstring> fils;
	string hh = hs(t.info_hash());
	size_t n = t.torrent_file()->files().num_files();

	string tn = t.status().name;
	for (int iif = 0; iif < n; iif++)
	{
		ystring np = Setting("TORRENTDIR", ".\\TORRENTS").c_str();
		np += L"\\";
		ystring np2 = t.torrent_file()->files().file_path(iif).c_str();
		np += np2;
		fils.push_back(np);
	}

	scanqueue.writelock([&](queue<tuple<string, vector<wstring>,string>>& vv) {

		tuple<string, vector<wstring>,string> a;
		get<0>(a) = hh;
		get<1>(a) = fils;
		get<2>(a) = tn;

		vv.push(a);
	});

	/*
		std::thread tt([](vector<wstring> files,string hash) {

			HAMSICONTEXT h = 0;
			if (FAILED(AmsiInitialize(ttitle, &h)))
				return;

			bool M = false;
			size_t n = files.size();
			for (int iif = 0; iif < n; iif++)
			{
				MMFILE m(files[iif].c_str());
				if (m.size() == 0)
					continue;
				AMSI_RESULT ar;
				if (FAILED(AmsiScanBuffer(h, (PVOID)m.operator const char *(), m.size(), files[iif].c_str(), 0, &ar)))
					continue;
				if (AmsiResultIsMalware(ar))
				{
					M = true;
					break;
				}
			}


			AmsiUninitialize(h);
			if (M)
				MessageBox(MainWindow, L"Torrent contains malware", ttitle, MB_OK);
			else
			{
				sqlite::sqlite sqlx("config.db");
				sqlite::query q(sqlx.h(), "UPDATE TORRENTS SET SCANNED = 1 WHERE HASH = ?");
				q.BindText(1, hash.c_str(), hash.length());
				q.R();
			}

	//		SendMessage(MainWindow, WM_USER + 552, 0,(LPARAM) &sst);

		},fils,hh);
		tt.detach();


		*/


}

wstring PrevLV;
void UpdateListView2(lt::torrent_status* st)
{
	using namespace winrt::Windows::UI::Xaml::Markup;
	if (!st)
		return;

//	auto idx = st->handle.id();
	string ha = hs(st->handle.info_hash());



	TopView nv = c->ins.as<TopView>();
	StackPanel pvs = nv.FindName(L"pivotstack").as<StackPanel>();

	ystring fn;
	fn.Format(L"%S", ha.c_str());

	

	// Update the ListView
	try
	{
		auto lv = nv.FindName(L"torrlist").as<ListView>();
		auto its = lv.Items();
		for (uint32_t i = 0; i < its.Size(); i++)
		{
			auto sp = its.GetAt(i).as<StackPanel>();
			ystring n =  winrt::unbox_value<winrt::hstring>(sp.Tag()).c_str();
			if (n != fn)
				continue;


			// Pivot
			auto tf = st->handle.torrent_file();
			Pivot pitt = pvs.Children().GetAt(0).as<Pivot>();

			if (n == LVSelected)
			{
				auto SaveP = PrevLV;
				PrevLV = LVSelected;
				if (tf)
				{
					pitt.Visibility(Xaml::Visibility::Visible);

					if (LVSelected != SaveP)
					{
						auto Info = pitt.Items().GetAt(0).as<PivotItem>();
						auto ha = hs(st->info_hash);

						bool Finished = false;
						if (st->state == lt::torrent_status::seeding)
							Finished = true;
						// Create info


							
						auto pi = sp.FindName(ystring().Format(L"PI%S", ha.c_str())).as<TextBlock>();
						auto jpos = st->handle.queue_position();
						if (!Finished)
							pi.Text(ystring().Format(L"%u", jpos));

						if (Finished)
						{
							auto i1 = LR"(
					<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
			
					<RichTextBlock>
						<Paragraph>Name: <Run FontStyle="Italic" FontWeight="Bold">%S</Run></Paragraph>
					</RichTextBlock>

					<StackPanel Orientation="Horizontal" Margin="0,10,0,0">
						<Button Content="Open folder" Name="OP%S" Margin="0,0,10,0"/>
						<Button Content="Delete" Name="MD%S" Margin="0,0,10,0"/>
					</StackPanel>
				
					</StackPanel>
)";

							ystring s1;
							s1.Format(i1, st->name.c_str(),  ha.c_str(),ha.c_str());

							auto x1 = XamlReader::Load(s1);
							Info.Content(x1);
						}
						else
						{
							auto i1 = LR"(
					<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
			
					<RichTextBlock>
						<Paragraph>Name: <Run FontStyle="Italic" FontWeight="Bold">%S</Run></Paragraph>
						<Paragraph>Size: <Run FontStyle="Italic" FontWeight="Bold">%.1f MB</Run></Paragraph>
					</RichTextBlock>

					<StackPanel Orientation="Horizontal" Margin="0,10,0,0">
						<Button Content="Open folder" Name="OP%S" Margin="0,0,10,0"/>
						<Button Content="Pause/Resume" Name="PR%S" Margin="0,0,10,0"/>
<Button Content="Priority"  Margin="0,0,10,0">
    <Button.Flyout>
        <MenuFlyout>
            <MenuFlyoutItem Text="Top" Name="P0_%S"/>
            <MenuFlyoutItem Text="Up" Name="P1_%S"/>
            <MenuFlyoutItem Text="Down" Name="P2_%S"/>
            <MenuFlyoutItem Text="Bottom"  Name="P3_%S"/>
        </MenuFlyout>
    </Button.Flyout>
</Button>
						<Button Content="Delete" Name="MD%S" Margin="0,0,10,0"/>
					</StackPanel>
				
					</StackPanel>
)";

							ystring s1;
							s1.Format(i1, st->name.c_str(), (float)(st->total / 1048576.0f), ha.c_str(), ha.c_str(), ha.c_str(),
								 ha.c_str(), ha.c_str(), ha.c_str(), ha.c_str()
							);

							auto x1 = XamlReader::Load(s1);
							Info.Content(x1);
						}

						// PRi Handler
						if (!Finished)
						{
							Info.Content().as<StackPanel>().FindName(ystring().Format(L"P0_%S", ha.c_str())).as<MenuFlyoutItem>().Click(
								[](const IInspectable& sender, const RoutedEventArgs& r)
							{
								string hash = ystring(sender.as<MenuFlyoutItem>().Name().c_str() + 3).a_str();
								th.readlock([&](const vector<lt::torrent_handle>& v) {
									for (auto& t : v)
									{
										if (hs(t.info_hash()) == hash)
										{
											t.queue_position_top();
										}
									}
								});
							});
							Info.Content().as<StackPanel>().FindName(ystring().Format(L"P1_%S", ha.c_str())).as<MenuFlyoutItem>().Click(
								[](const IInspectable& sender, const RoutedEventArgs& r)
							{
								string hash = ystring(sender.as<MenuFlyoutItem>().Name().c_str() + 3).a_str();
								th.readlock([&](const vector<lt::torrent_handle>& v) {
									for (auto& t : v)
									{
										if (hs(t.info_hash()) == hash)
										{
											t.queue_position_up();
										}
									}
								});
							});
							Info.Content().as<StackPanel>().FindName(ystring().Format(L"P2_%S", ha.c_str())).as<MenuFlyoutItem>().Click(
								[](const IInspectable& sender, const RoutedEventArgs& r)
							{
								string hash = ystring(sender.as<MenuFlyoutItem>().Name().c_str() + 3).a_str();
								th.readlock([&](const vector<lt::torrent_handle>& v) {
									for (auto& t : v)
									{
										if (hs(t.info_hash()) == hash)
										{
											t.queue_position_down();
										}
									}
								});
							});
							Info.Content().as<StackPanel>().FindName(ystring().Format(L"P3_%S", ha.c_str())).as<MenuFlyoutItem>().Click(
								[](const IInspectable& sender, const RoutedEventArgs& r)
							{
								string hash = ystring(sender.as<MenuFlyoutItem>().Name().c_str() + 3).a_str();
								th.readlock([&](const vector<lt::torrent_handle>& v) {
									for (auto& t : v)
									{
										if (hs(t.info_hash()) == hash)
										{
											t.queue_position_bottom();
										}
									}
								});
							});

							// PR Handler
							Info.Content().as<StackPanel>().FindName(ystring().Format(L"PR%S", ha.c_str())).as<Button>().Click(
								[](const IInspectable& sender, const RoutedEventArgs& r)
							{
								Button mf = sender.as<Button>();
								wstring t = mf.Name().c_str();
								string hash = ystring(t.c_str() + 2).a_str();


								th.readlock([&](const vector<lt::torrent_handle>& v) {
									for (auto& t : v)
									{
										if (hs(t.info_hash()) == hash)
										{
											// Resume this torrent
											reqs.writelock([&](queue<TREQUEST>& r)
											{
												TREQUEST rr;
												rr.Type = 2;
												if (t.status().paused)
													rr.Type = 3;
												rr.h = hash;
												r.push(rr);
											});

										}
									}
								});
							}
							);
						}


						// Delete Handler
						Info.Content().as<StackPanel>().FindName(ystring().Format(L"MD%S", ha.c_str())).as<Button>().Click(
							[](const IInspectable& sender, const RoutedEventArgs& r)
						{
							Button mf = sender.as<Button>();
							wstring t = mf.Name().c_str();
							DeleteTorrent(ystring(t.c_str() + 2).a_str());
						}
						);

						// Open Folder Handler
						Info.Content().as<StackPanel>().FindName(ystring().Format(L"OP%S", ha.c_str())).as<Button>().Click(
							[](const IInspectable& sender, const RoutedEventArgs& r)
						{
							Button mf = sender.as<Button>();
							wstring t = mf.Name().c_str();
							string hash = ystring(t.c_str() + 2).a_str();
							th.readlock([&](const vector<lt::torrent_handle>& v) {
								for (auto& t : v)
								{
									if (hs(t.info_hash()) == hash)
									{
										ystring pa = t.status().save_path;
										ShellExecute(MainWindow, L"open", pa.c_str(), 0, 0, SW_SHOWNORMAL);
									}
								}
							});

						}
						);


					}

					// Files
					if (LVSelected != SaveP)
					{
						auto Files = pitt.Items().GetAt(1).as<PivotItem>();
						auto LFiles = Files.FindName(L"FilesView").as<ListView>();
						LFiles.Items().Clear();
						if (tf)
						{
							size_t n = tf->files().num_files();
							for (int iif = 0; iif < n; iif++)
							{
								auto np = tf->files().file_path(iif);
//								auto nn = tf->files().file_name(iif);

								// Remove &
								for (auto& aa : np)
								{
									if (aa == '&')
										aa = '_';
								}


								auto i2 = LR"(
					<StackPanel xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
 xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
					<TextBlock Text="%S" />
					</StackPanel>
)";

								ystring s2;
								s2.Format(i2, np.c_str());
								try
								{
									auto x2 = XamlReader::Load(s2);
									LFiles.Items().Append(x2);
								}
								catch (...)
								{
									MessageBox(0, s2.c_str(), 0, 0);
								}
							}
						}
					}
				}
				else
					pitt.Visibility(Xaml::Visibility::Collapsed);
			}
			

			auto prg = sp.FindName(ystring().Format(L"Prg%S", ha.c_str())).as<ProgressBar>();
			auto ra = sp.FindName(ystring().Format(L"RA%S", ha.c_str())).as<TextBlock>();
			ra.Text(L"");
			if (st->paused)
			{
				prg.ShowPaused(true);
				prg.IsIndeterminate(true);
			}
			else
			if (st->state == lt::torrent_status::downloading)
			{
				prg.ShowPaused(false);
				prg.IsIndeterminate(false);
				prg.Maximum((double)st->total);
				prg.Value((double)st->total_done);

				auto kbt = sp.FindName(ystring().Format(L"KB%S", ha.c_str())).as<TextBlock>();
				int Perc = (int)((100 * st->total_done) / st->total);
				if (Perc >= 100)	
					kbt.Text(ystring().Format(L"Finishing... %.1f/%.1f MB", st->total_done / 1048576.0f, st->total / 1048576.0f).c_str());
				else
					kbt.Text(ystring().Format(L"%u%% %.1f/%.1f MB", Perc, st->total_done / 1048576.0f, st->total / 1048576.0f).c_str());

				ra.Text(ystring().Format(L"%.1f KB/s", st->download_rate/ 1024.0f));

			}
			else
			if (st->state == lt::torrent_status::seeding)
			{
				prg.ShowPaused(false);
				prg.IsIndeterminate(false);
				prg.Value(prg.Maximum());
				sp.FindName(ystring().Format(L"KB%S", ha.c_str())).as<TextBlock>().Text(L"Finished");
			}
			else
			{
				prg.ShowPaused(false);
				prg.IsIndeterminate(true);
				if (st->state == lt::torrent_status::checking_files)
					sp.FindName(ystring().Format(L"KB%S", ha.c_str())).as<TextBlock>().Text(L"Checking files...");
				else
				if (st->state == lt::torrent_status::downloading_metadata)
					sp.FindName(ystring().Format(L"KB%S", ha.c_str())).as<TextBlock>().Text(L"Getting metadata...");
				else
					sp.FindName(ystring().Format(L"KB%S", ha.c_str())).as<TextBlock>().Text(L"Preparing...");
			}

//			if (st->state != lt::torrent_status::downloading)
			{
				auto txt = sp.FindName(ystring().Format(L"Text%S", ha.c_str())).as<TextBlock>();
				txt.Text(ystring(st->name.c_str()).c_str());
			}
		}
	}
	catch(...)
	{
	
	}
}


void foo()
{
/*	lt::add_torrent_params p;
	p.save_path = "./";
	p.ti = std::make_shared<lt::torrent_info>(std::string("ha"));
	s.add_torrent(p);
	*/
}


void AddTorrentFile(const wchar_t* f,bool CheckMutex)
{
	if (!f)
		return;
	ystring url = f;
	// Check if there?
	sqlite::query q(sql->h(), "SELECT * FROM TORRENTS WHERE FILE = ?");
	q.BindText(1, url.a_str(), strlen(url.a_str()));
	map<string, string> row;
	if (!q.NextRow(row))
	{
		// Add it
		sqlite::query q2(sql->h(), "INSERT INTO TORRENTS (FILE) VALUES (?)");
		q2.BindText(1, url.a_str(), strlen(url.a_str()));
		q2.R();

		if (CheckMutex)
		{
			auto hm = OpenMutex(SYNCHRONIZE, false, mutn);
			if (hm)
			{
				CloseHandle(hm);
				SendMessage(HWND_TOPMOST, umsg, 1, 0);
				return;
			}
		}


		// Add it to queue
		reqs.writelock([&](queue<TREQUEST>& r)
		{
			TREQUEST rr;
			rr.Type = 5;
			rr.t = url;
			rr.row = sql->last();
			r.push(rr);
		});
	}

}


ystring xaml;

#ifdef USE_NAVIGATIONVIEW

void ShowMainView()
{
	NavigationView nv = c->ins.as<NavigationView>();
	auto spm = c->ins.as<TopView>().FindName(L"MainView").as<StackPanel>();
	auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
	auto scn = c->ins.as<TopView>().FindName(L"AVResults").as<StackPanel>();
	sp.Visibility(Visibility::Collapsed);
	spm.Visibility(Visibility::Visible);
	scn.Visibility(Visibility::Collapsed);
	nv.IsBackEnabled(false);
}


void ShowAVScan()
{
	NavigationView nv = c->ins.as<NavigationView>();
	auto spm = c->ins.as<TopView>().FindName(L"MainView").as<StackPanel>();
	auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
	auto scn = c->ins.as<TopView>().FindName(L"AVResults").as<StackPanel>();
	spm.Visibility(Visibility::Collapsed);
	scn.Visibility(Visibility::Visible);
	sp.Visibility(Visibility::Collapsed);
	nv.IsBackEnabled(true);
}

void ShowSettings()
{
	NavigationView nv = c->ins.as<NavigationView>();
	auto spm = c->ins.as<TopView>().FindName(L"MainView").as<StackPanel>();
	auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
	auto scn = c->ins.as<TopView>().FindName(L"AVResults").as<StackPanel>();
	spm.Visibility(Visibility::Collapsed);
	sp.Visibility(Visibility::Visible);
	scn.Visibility(Visibility::Collapsed);
	nv.IsBackEnabled(true);
}

void ItemInvoked(const IInspectable& nav, const NavigationViewItemInvokedEventArgs& r)
{
	NavigationView nv = c->ins.as<NavigationView>();

	auto it = r.InvokedItemContainer().as<NavigationViewItem>();
	auto tag = it.Content();
	if (!tag)
	{
		ShowSettings();
		return;
	}
	ystring str = winrt::unbox_value<winrt::hstring>(tag).c_str();
	if (str == L"Torrents")
	{
		ShowMainView();
	}
	if (str == L"AV Results")
	{
		ShowAVScan();
	}
}

void SaveSettings()
{
	// Save settings...
	NavigationView nv = c->ins.as<NavigationView>();
	auto T_TCPPort = nv.FindName(L"T_TCPPort").as<TextBox>();
	int PortNum = _wtoi(T_TCPPort.Text().c_str());
	Setting("TCPPORT", ystring().Format(L"%u", PortNum).a_str(), true);

	Setting("UPLOADLIMIT",ystring().Format(L"%u",_wtoi(nv.FindName(L"T_UploadLimit").as<TextBox>().Text().c_str())).a_str(),true);
	Setting("DOWNLOADLIMIT", ystring().Format(L"%u", _wtoi(nv.FindName(L"T_DownloadLimit").as<TextBox>().Text().c_str())).a_str(), true);

}

void BackRequested(const IInspectable& nav, const NavigationViewBackRequestedEventArgs& r)
{
	SaveSettings();
	ShowMainView();
}
#endif


void ViewMain()
{

	if (xaml.empty())
	{
		auto hm = GetModuleHandle(0);
		auto h1 = FindResource(hm, mvx.c_str(), L"DATA");
		if (h1)
		{
			auto h2 = LoadResource(hm, h1);
			if (h2)
			{
				auto h3 = LockResource(h2);
				auto sz = SizeofResource(hm, h1);
				vector<char> x(sz + 1);
				memcpy(x.data(), h3, sz);
				xaml = x.data();
			}
		}
	}
	if (xaml.empty())
		return;
	

	ystring pp1;
	pp1.Format(xaml.c_str(), Setting("TORRENTDIR", ".\\TORRENTS").c_str());

	SetWindowText(hX, pp1.c_str());
	c = (UWPLIB::UWPCONTROL*)SendMessage(hX, UWPM_GET_CONTROL, 0, 0);
	TopView x1 = c->ins.as<TopView>();

#ifdef USE_NAVIGATIONVIEW
	x1.ItemInvoked(ItemInvoked);
	x1.BackRequested(BackRequested);
#endif

	auto PickTorrentDirB = x1.FindName(L"TB_SaveDirB").as<Button>();
	PickTorrentDirB.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		wchar_t rv[1000] = { 0 };
		wcscpy_s(rv, 1000, Setting("TORRENTDIR", ".\\TORRENTS").c_str());
		if (!BrowseFolder(0, L"", 0, rv, rv))
			return;

		c = (UWPLIB::UWPCONTROL*)SendMessage(hX, UWPM_GET_CONTROL, 0, 0);
		TopView x1 = c->ins.as<TopView>();
		Setting("TORRENTDIR", ystring(rv).a_str(),true);

		auto PickTorrentDir = x1.FindName(L"TB_SaveDirB").as<Button>();
		PickTorrentDir.Content(winrt::box_value(ystring().Format(L"Saving to: %s", rv).c_str()));
	});

	auto PickTorrentDir = x1.FindName(L"TB_SaveDirB").as<Button>();
	PickTorrentDir.Content(winrt::box_value(ystring().Format(L"Saving to: %s", Setting("TORRENTDIR", ".\\TORRENTS").c_str())));


#ifndef USE_NAVIGATIONVIEW
	auto SettingsBack = x1.FindName(L"btnoptionsback").as<Button>();
	SettingsBack.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		auto spm = c->ins.as<TopView>().FindName(L"MainView").as<StackPanel>();
		auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
		sp.Visibility(Visibility::Collapsed);
		spm.Visibility(Visibility::Visible);
	});
#endif

#ifndef USE_NAVIGATIONVIEW
	auto SettingsB = x1.FindName(L"btns").as<Button>();
	SettingsB.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		auto spm = c->ins.as<TopView>().FindName(L"MainView").as<StackPanel>();
		auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
		if (sp.Visibility() == Visibility::Visible)
		{
			sp.Visibility(Visibility::Collapsed);
			spm.Visibility(Visibility::Visible);
		}
		else
		{
			sp.Visibility(Visibility::Visible);
			spm.Visibility(Visibility::Collapsed);
		}
	});
#endif

		// Magnet Link Button
	auto sp = c->ins.as<TopView>().FindName(L"Options").as<StackPanel>();
	auto cbi = sp.FindName(L"CB_MagnetLink").as<CheckBox>();
	if (IsMagnet())
		cbi.IsChecked(true);
	cbi.Checked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		auto cbi = sender.as<CheckBox>();
		if (!IsMagnet())
		{
			TCHAR fi[1000];
			GetModuleFileName(GetModuleHandle(0), fi, 1000);
			RunAsAdmin(0, fi, L"/minstall");
			if (IsMagnet())
				cbi.IsChecked(true);
			else
				cbi.IsChecked(false);
		}
	});
	cbi.Unchecked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		auto cbi = sender.as<CheckBox>();
		if (IsMagnet())
		{
			TCHAR fi[1000];
			GetModuleFileName(GetModuleHandle(0), fi, 1000);
			RunAsAdmin(0, fi, L"/muninstall");
			if (IsMagnet())
				cbi.IsChecked(true);
			else
				cbi.IsChecked(false);
		}
	});

	// Torrent Button
	auto mbi = sp.FindName(L"CB_TorrentFile").as<CheckBox>();
	if (IsTorrent())
		mbi.IsChecked(true);
	mbi.Checked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		auto mbi = sender.as<CheckBox>();
		if (!IsTorrent())
		{
			TCHAR fi[1000];
			GetModuleFileName(GetModuleHandle(0), fi, 1000);
			RunAsAdmin(0, fi, L"/tinstall");
			if (IsTorrent())
				mbi.IsChecked(true);
			else
				mbi.IsChecked(false);
		}
	});
	mbi.Unchecked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		auto mbi = sender.as<CheckBox>();
		if (IsTorrent())
		{
			TCHAR fi[1000];
			GetModuleFileName(GetModuleHandle(0), fi, 1000);
			RunAsAdmin(0, fi, L"/tuninstall");
			if (IsTorrent())
				mbi.IsChecked(true);
			else
				mbi.IsChecked(false);
		}
	});


	// Torrent Button
	auto avi = sp.FindName(L"CB_AV").as<CheckBox>();
	if (Setting("SCANFINISHED","0") == ystring("1"))
		avi.IsChecked(true);
	avi.Checked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		Setting("SCANFINISHED", "1", true);
	});
	avi.Unchecked([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		Setting("SCANFINISHED", "0", true);
	});


	auto AddF = x1.FindName(L"btnf").as<Button>();
	AddF.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		OPENFILENAME of = { 0 };
		of.lStructSize = sizeof(of);
		of.hwndOwner = MainWindow;
		of.lpstrFilter = L"*.torrent\0*.torrent\0\0";
		of.lpstrInitialDir = 0;
		of.nFilterIndex = 0;
		wchar_t fnx[10000] = { 0 };;
		of.lpstrFile = fnx;
		of.nMaxFile = 10000;
		of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
		if (!GetOpenFileName(&of))
			return;
		TCHAR* ff = fnx + of.nFileOffset;
		ff--;
		if (*ff != 0)
		{
			// fnx
			AddTorrentFile(fnx,false);
		}
		else
		{
			ff++;
			wchar_t dir[1000] = { 0 };
			wcscpy_s(dir, 1000, fnx);
			wchar_t f[1000] = { 0 };
			for (;;)
			{
				swprintf_s(f, 1000, L"%s\\%s", dir, ff);

				AddTorrentFile(f,false);

				ff += _tcslen(ff);
				ff++;
				if (*ff == 0)
					break;
			}
		}

	});

	auto sortfunc = [](const IInspectable& ins, const RoutedEventArgs& r)
	{
		TopView tv = c->ins.as<TopView>();
		auto lv = tv.FindName(L"torrlist").as<ListView>();
		lv.Items().Clear();

		vector<lt::torrent_handle> ex;
		th.readlock([&](const vector<lt::torrent_handle>& e) {
			ex = e;
		});

		// Sort ex
		wstring n =ins.as<MenuFlyoutItem>().Name().c_str();
		if (n == wstring(L"sort_name"))
		{
			std::sort(ex.begin(), ex.end(), [](const lt::torrent_handle& h1, const lt::torrent_handle& h2) -> bool
			{
				if (h1.status().name < h2.status().name)
					return true;
				return false;
			});
		}
		if (n == wstring(L"sort_priority"))
		{
			std::sort(ex.begin(), ex.end(), [](const lt::torrent_handle& h1, const lt::torrent_handle& h2) -> bool
			{
				if (h1.queue_position() < h2.queue_position())
					return true;
				return false;
			});
		}

		for (auto& ee : ex)
			UpdateListView(&ee, 0);

	};

	x1.FindName(L"sort_name").as<MenuFlyoutItem>().Click(sortfunc);
	x1.FindName(L"sort_priority").as<MenuFlyoutItem>().Click(sortfunc);

	auto AddURL = x1.FindName(L"btn1").as<Button>();
	AddURL.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		TopView sp = c->ins.as<TopView>();
		auto dlg = sp.FindName(L"Dlg1").as<ContentDialog>();
		auto apo = dlg.ShowAsync();

		auto magnetButtonCancel = dlg.FindName(L"btn3").as<Button>();
		magnetButtonCancel.Click([](const IInspectable& ins, const RoutedEventArgs& r)
		{
			TopView sp = c->ins.as<TopView>();
			auto dlg = sp.FindName(L"Dlg1").as<ContentDialog>();
			dlg.Hide();
		});


		auto magnetButtonOK = dlg.FindName(L"btn2").as<Button>();
		magnetButtonOK.Click([](const IInspectable& ins, const RoutedEventArgs& r)
		{
			TopView sp = c->ins.as<TopView>();
			ystring url = sp.FindName(L"murl").as<TextBox>().Text().c_str();
			auto dlg = sp.FindName(L"Dlg1").as<ContentDialog>();
			dlg.Hide();

			// Check if there?
			sqlite::query q(sql->h(), "SELECT * FROM TORRENTS WHERE MAGNET = ?");
			q.BindText(1, url.a_str(), strlen(url.a_str()));
			map<string, string> row;
			if (!q.NextRow(row))
			{
				// Add it
				sqlite::query q2(sql->h(), "INSERT INTO TORRENTS (MAGNET) VALUES (?)");
				q2.BindText(1, url.a_str(), strlen(url.a_str()));
				q2.R();

				// Add it to queue
				reqs.writelock([&](queue<TREQUEST>& r)
				{
					TREQUEST rr;
					rr.Type = 1;
					rr.t = url;
					rr.row = sql->last();
 					r.push(rr);
				});
			}


		});

/*		apo.Completed([](IAsyncOperation<ContentDialogResult> const& sender, const AsyncOperationCompletedHandler<ContentDialogResult>& res)
		{
			MessageBox(0, 0, 0, 0);
		}
		);
*/
		return;
	});

	

	auto lv = x1.FindName(L"torrlist").as<ListView>();

	lv.SelectionChanged([](const IInspectable&  sender, const RoutedEventArgs&)
	{
		TopView nv = c->ins.as<TopView>();
		auto lv = sender.as<ListView>();
		unsigned int idx = lv.SelectedIndex();
		auto s = LVSelected;
		LVSelected.clear();
		if (lv.SelectedItems().Size() != 1)
		{
			nv.FindName(L"MainView2").as<StackPanel>().Orientation(Orientation::Vertical);
			StackPanel pvs = nv.FindName(L"pivotstack").as<StackPanel>();
			Pivot pitt = pvs.Children().GetAt(0).as<Pivot>();
			pitt.Visibility(Visibility::Collapsed);
			return;
		}

		if (idx < lv.Items().Size())
		{
			// Get StackPanel idx
			nv.FindName(L"MainView2").as<StackPanel>().Orientation(Orientation::Horizontal);
			auto spx = lv.Items().GetAt(idx).as<StackPanel>();
			ystring n = winrt::unbox_value<winrt::hstring>(spx.Tag()).c_str();
			LVSelected = n;
			if (s == LVSelected)
			{
/*				LVSelected.clear();
				lv.SelectedItems().Clear();
				// Hide the pivot
				StackPanel pvs = nv.FindName(L"pivotstack").as<StackPanel>();
				Pivot pitt = pvs.Children().GetAt(0).as<Pivot>();
				pitt.Visibility(Visibility::Collapsed);
	*/		}
		}
		else
		{
			nv.FindName(L"MainView2").as<StackPanel>().Orientation(Orientation::Vertical);
			StackPanel pvs = nv.FindName(L"pivotstack").as<StackPanel>();
			Pivot pitt = pvs.Children().GetAt(0).as<Pivot>();
			pitt.Visibility(Visibility::Collapsed);
		}
	

	});

	// Port Number
	auto T_TCPPort = x1.FindName(L"T_TCPPort").as<TextBox>();
	ystring PortNum = Setting("TCPPORT", "7008");
	T_TCPPort.Text(PortNum);

	auto ApplyS = x1.FindName(L"ApplySettingsButton").as<Button>();
	ApplyS.Click([](const IInspectable& ins, const RoutedEventArgs& r)
	{
		SaveSettings();
	});

	// Other options
	x1.FindName(L"T_UploadLimit").as<TextBox>().Text(ystring().Format(L"%u",_wtoi(Setting("UPLOADLIMIT","0").c_str())));
	x1.FindName(L"T_DownloadLimit").as<TextBox>().Text(ystring().Format(L"%u", _wtoi(Setting("DOWNLOADLIMIT", "0").c_str())));


}


LRESULT CALLBACK Main_DP(HWND hh, UINT mm, WPARAM ww, LPARAM ll)
	{
	if (mm == umsg)
	{
		if (ww == 1) // Refresh torrents
		{
			SetForegroundWindow(hh);
			// Check if there?
			sqlite::query q(sql->h(), "SELECT * FROM TORRENTS WHERE HASH IS NULL");
			map<string, string> row;
			while(q.NextRow(row))
			{
				// Add it to queue
				reqs.writelock([&](queue<TREQUEST>& r)
				{
					TREQUEST rr;
					if (strlen(row["MAGNET"].c_str()))
					{
						rr.Type = 1;
						rr.t = row["MAGNET"];
						rr.row = atoi(row["ID"].c_str());
						r.push(rr);
					}
					if (strlen(row["FILE"].c_str()))
					{
						rr.Type = 5;
						rr.t = row["FILE"];
						rr.row = atoi(row["ID"].c_str());
						r.push(rr);
					}
				});
			}

		}
		return 0;
	}

	switch (mm)
		{
		case WM_USER + 301:
		{
			if (ll == WM_LBUTTONDBLCLK)
			{
				ShowWindow(hh, SW_SHOW);
				SetForegroundWindow(hh);
			}
			return 0;
		}
		case WM_USER + 551:
		{
			lt::torrent_handle* h2 = (lt::torrent_handle*)ll;
			UpdateListView(h2);
			return 0;
		}
		case WM_USER + 553:
		{
			lt::torrent_handle* h2 = (lt::torrent_handle*)ll;
			UpdateListView(h2,ww);
			return 0;
		}
		case WM_USER + 552:
		{
			lt::torrent_status* h2 = (lt::torrent_status*)ll;
			UpdateListView2(h2);
			return 0;
		}

		case WM_USER + 554: // set Scan text
		{
			TopView nv = c->ins.as<TopView>();
			if (ll)
			{
				auto ins = nv.FindName(L"ScanText");
				if (ins)
					ins.as<TextBlock>().Text((wchar_t*)ll);
			}
			if (ww)
			{
				auto ins = nv.FindName(L"scanlist");
				if (ins)
				{
					auto lv = ins.as<ListView>();

					ystring sp;
					sp = ystring().Format(
						LR"(
					<TextBlock xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Margin="20,0,0,0" Text="%s" />
						)", (wchar_t*)ww);
					using namespace winrt::Windows::UI::Xaml::Markup;
					auto ins2 = XamlReader::Load(sp.c_str());
					lv.Items().Append(ins2);
					ShowAVScan();
				}
			}
			return 0;
		}

		case WM_CREATE:
			{
			hX = CreateWindowEx(0, L"UWP_Custom", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hh, (HMENU)901, 0, 0);
			SendMessage(hh, WM_SIZE, 0, 0);
			ViewMain();
			std::thread t(BitThread);			t.detach();
			std::thread tt(AVThread);			tt.detach();
			break;
			}

		case WM_SIZE:
		{
			RECT rc;
			GetClientRect(hh, &rc);
			if (ww == SIZE_MINIMIZED)
			{
				ShowWindow(hh, SW_HIDE);
				return 0;
			}
			SetWindowPos(hX, 0, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
			if (c)
				SetWindowPos(c->hwndDetailXamlIsland, 0, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
			return 0;
		}

		case WM_COMMAND:
			{
			int LW = LOWORD(ww);
			UNREFERENCED_PARAMETER(LW);


			return 0;
			}

		case WM_CLOSE:
			{
			DestroyWindow(hX);
			DestroyWindow(hh);
			return 0;
			}

		case WM_DESTROY:
			{
			PostQuitMessage(0);
			return 0;
			}
		}
	return DefWindowProc(hh, mm, ww, ll);
	}




TRAY tr;
int __stdcall WinMain(HINSTANCE h, HINSTANCE, LPSTR t, int)
	{
	TCHAR cd[1000];
	GetModuleFileName(h, cd, 1000);
	TCHAR* r1 = wcsrchr(cd, '\\');
	if (r1)
	{
		*r1 = 0;
		SetCurrentDirectory(cd);
	}

	WSADATA wData;
	WSAStartup(MAKEWORD(2, 2), &wData);
	CoInitializeEx(0, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icex = { 0 };
	icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_DATE_CLASSES | ICC_WIN95_CLASSES;
	icex.dwSize = sizeof(icex);
	InitCommonControlsEx(&icex);
	InitCommonControls();
	hIcon1 = LoadIcon(h, _T("ICON_1"));
#ifdef PRE_BUILD
	Update();
#endif


	hAppInstance = h;
	sql = make_shared<sqlite::sqlite>("config.db");
	SQLPrep();
//	ttest();
	umsg = RegisterWindowMessage(mutn);



	if (t && strlen(t))
	{
		if (strstr(t, "/minstall") != 0)
		{
			MagnetRegister();
			return 0;
		}

		if (strstr(t, "/muninstall") != 0)
		{
			MagnetUnregister();
			return 0;
		}

		if (strstr(t, "/tinstall") != 0)
		{
			TorrentRegister();
			return 0;
		}

		if (strstr(t, "/tuninstall") != 0)
		{
			TorrentUnregister();
			return 0;
		}

		if (strstr(t, "magnet:") != 0)
		{
			// Add magnet link
			ystring url = t;
			url.erase(url.end() - 1);
			url.erase(url.begin());
			sqlite::query q(sql->h(), "SELECT * FROM TORRENTS WHERE MAGNET = ?");
			q.BindText(1, url.a_str(), strlen(url.a_str()));
			map<string, string> row;
			if (!q.NextRow(row))
			{
				// Add it
				sqlite::query q2(sql->h(), "INSERT INTO TORRENTS (MAGNET) VALUES (?)");
				q2.BindText(1, url.a_str(), strlen(url.a_str()));
				q2.R();
			}
			auto hm = OpenMutex(SYNCHRONIZE, false, mutn);
			if (hm)
			{
				CloseHandle(hm);
				SendMessage(HWND_TOPMOST, umsg, 1, 0);
				return 0;
			}
		}
		else
		{
			// Add Torrent File
			ystring url = t;
			url.erase(url.end() - 1);
			url.erase(url.begin());
			AddTorrentFile(url.c_str(), true);
			auto hm = OpenMutex(SYNCHRONIZE, false, mutn);
			if (hm)
			{
				CloseHandle(hm);
				SendMessage(HWND_TOPMOST, umsg, 1, 0);
				return 0;
			}
		}
	}



	auto hm = OpenMutex(SYNCHRONIZE, false, mutn);
	if (hm != 0)
	{
		MessageBox(0, L"FluentTorrent is already running.", ttitle, MB_OK);
		return 0;
	}

	hm = CreateMutex(0, true, mutn);

	WNDCLASSEX wClass = { 0 };
	wClass.cbSize = sizeof(wClass);

	UWPLIB::Register();


	wClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW | CS_PARENTDC;
	wClass.lpfnWndProc = (WNDPROC)Main_DP;
	wClass.hInstance = h;
	wClass.hIcon = hIcon1;
	wClass.hCursor = LoadCursor(0, IDC_ARROW);
	wClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wClass.lpszClassName = _T("CLASS");
	wClass.hIconSm = hIcon1;
	RegisterClassEx(&wClass);

	MainWindow = CreateWindowEx(0,
		_T("CLASS"),
		ttitle,
		WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS |
		WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, h, 0);

	ShowWindow(MainWindow, SW_SHOW);

	tr.Attach(MainWindow, hIcon1, WM_USER + 301);

	MSG msg;

	while (GetMessage(&msg, 0, 0, 0))
		{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		}

	sql = 0;
	End = true;
	for (int i = 0; i < 10; i++)
	{
		if (EndT1 && EndT2)
			break;
		Sleep(1000);
	}
	CloseHandle(hm);
	return 0;
}