// Copyright (c) 2012-2018 The Elastos Open Source Project
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "SpvService.h"

#include <SDK/Common/Utils.h>
#include <SDK/Common/Log.h>
#include <SDK/Plugin/Transaction/Asset.h>
#include <SDK/Plugin/Registry.h>
#include <SDK/Plugin/Block/MerkleBlock.h>

#include <Core/BRMerkleBlock.h>
#include <Core/BRTransaction.h>

#include <boost/thread.hpp>

#define BACKGROUND_THREAD_COUNT 1

#define DATABASE_PATH "spv_wallet.db"
#define ISO "ela"

namespace Elastos {
	namespace ElaWallet {

		SpvService::SpvService(const SubAccountPtr &subAccount, const boost::filesystem::path &dbPath,
							   time_t earliestPeerTime, uint32_t reconnectSeconds,
							   const PluginType &pluginTypes, const ChainParamsPtr &chainParams) :
				CoreSpvService(pluginTypes, chainParams),
				_executor(BACKGROUND_THREAD_COUNT),
				_reconnectExecutor(BACKGROUND_THREAD_COUNT),
				_databaseManager(dbPath),
				_reconnectTimer(nullptr) {
			init(subAccount, earliestPeerTime, reconnectSeconds);
		}

		SpvService::~SpvService() {

		}

		void SpvService::Start() {
			getPeerManager()->SetReconnectEnableStatus(true);

			getPeerManager()->Connect();
		}

		void SpvService::Stop() {
			if (_reconnectTimer != nullptr) {
				_reconnectTimer->cancel();
				_reconnectTimer = nullptr;
			}

			getPeerManager()->SetReconnectTaskCount(0);
			getPeerManager()->SetReconnectEnableStatus(false);

			getPeerManager()->Disconnect();

			_executor.StopThread();
			_reconnectExecutor.StopThread();
		}

		void SpvService::PublishTransaction(const TransactionPtr &transaction) {
			nlohmann::json sendingTx = transaction->ToJson();
			ByteStream byteStream;
			transaction->Serialize(byteStream);

			Log::debug("{} publish tx {}", _peerManager->GetID(), sendingTx.dump());
			SPVLOG_DEBUG("raw tx {}", byteStream.GetBytes().getHex());

			if (getPeerManager()->GetConnectStatus() != Peer::Connected) {
				_peerManager->SetReconnectEnableStatus(false);
				if (_reconnectTimer != nullptr)
					_reconnectTimer->cancel();
				_peerManager->Disconnect();
				_peerManager->SetReconnectEnableStatus(true);
				getPeerManager()->Connect();
			}

			getPeerManager()->PublishTransaction(transaction);
		}

		const WalletPtr &SpvService::getWallet() {
			return CoreSpvService::getWallet();
		}

		//override Wallet listener
		void SpvService::balanceChanged(const uint256 &asset, const BigInt &balance) {
			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&asset, &balance](Wallet::Listener *listener) {
							  listener->balanceChanged(asset, balance);
						  });
		}

		void SpvService::onCoinBaseTxAdded(const CoinBaseUTXOPtr &cb) {
			CoinBaseUTXOEntity entity;

			entity.SetSpent(cb->Spent());
			entity.SetTxHash(cb->Hash().GetHex());
			entity.SetBlockHeight(cb->BlockHeight());
			entity.SetTimestamp(cb->Timestamp());
			entity.SetPayload(nullptr);
			entity.SetAmount(cb->Amount());
			entity.SetOutputLock(cb->OutputLock());
			entity.SetAssetID(cb->AssetID());
			entity.SetProgramHash(cb->ProgramHash());
			entity.SetIndex(cb->Index());

			_databaseManager.PutCoinBase(entity);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&cb](Wallet::Listener *listener) {
							  listener->onCoinBaseTxAdded(cb);
						  });
		}

		void SpvService::onCoinBaseTxUpdated(const std::vector<uint256> &hashes, uint32_t blockHeight,
											 time_t timestamp) {
			_databaseManager.UpdateCoinBase(hashes, blockHeight, timestamp);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&hashes, &blockHeight, &timestamp](Wallet::Listener *listener) {
							  listener->onCoinBaseTxUpdated(hashes, blockHeight, timestamp);
						  });
		}

		void SpvService::onCoinBaseSpent(const std::vector<uint256> &spentHashes) {
			_databaseManager.UpdateSpentCoinBase(spentHashes);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&spentHashes](Wallet::Listener *listener) {
							  listener->onCoinBaseSpent(spentHashes);
						  });
		}

		void SpvService::onCoinBaseTxDeleted(const uint256 &hash, bool notifyUser, bool recommendRescan) {
			_databaseManager.DeleteCoinBase(hash.GetHex());

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&hash, &notifyUser, &recommendRescan](Wallet::Listener *listener) {
							  listener->onCoinBaseTxDeleted(hash, notifyUser, recommendRescan);
						  });
		}

		void SpvService::onTxAdded(const TransactionPtr &tx) {
			ByteStream stream;
			tx->Serialize(stream);

			bytes_t data = stream.GetBytes();

			std::string txHash = tx->GetHash().GetHex();

			TransactionEntity txEntity(data, tx->GetBlockHeight(), tx->GetTimestamp(), txHash);
			_databaseManager.PutTransaction(ISO, txEntity);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&tx](Wallet::Listener *listener) {
							  listener->onTxAdded(tx);
						  });
		}

		void SpvService::onTxUpdated(const std::vector<uint256> &hashes, uint32_t blockHeight, time_t timestamp) {

			_databaseManager.UpdateTransaction(hashes, blockHeight, timestamp);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&hashes, &blockHeight, &timestamp](Wallet::Listener *listener) {
							  listener->onTxUpdated(hashes, blockHeight, timestamp);
						  });
		}

		void SpvService::onTxDeleted(const uint256 &hash, bool notifyUser, bool recommendRescan) {
			_databaseManager.DeleteTxByHash(ISO, hash.GetHex());

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&hash, &notifyUser, &recommendRescan](Wallet::Listener *listener) {
							  listener->onTxDeleted(hash, notifyUser, recommendRescan);
						  });
		}

		void SpvService::onAssetRegistered(const AssetPtr &asset, uint64_t amount, const uint168 &controller) {
			std::string assetID = asset->GetHash().GetHex();
			ByteStream stream;
			asset->Serialize(stream);
			AssetEntity assetEntity(assetID, amount, stream.GetBytes());
			_databaseManager.PutAsset(asset->GetName(), assetEntity);

			std::for_each(_walletListeners.begin(), _walletListeners.end(),
						  [&asset, &amount, &controller](Wallet::Listener *listener) {
							  listener->onAssetRegistered(asset, amount, controller);
						  });
		}

		//override PeerManager listener
		void SpvService::syncStarted() {
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [](PeerManager::Listener *listener) {
							  listener->syncStarted();
						  });
		}

		void SpvService::syncProgress(uint32_t currentHeight, uint32_t estimatedHeight, time_t lastBlockTime) {
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [&currentHeight, &estimatedHeight, &lastBlockTime](PeerManager::Listener *listener) {
				listener->syncProgress(currentHeight, estimatedHeight, lastBlockTime);
			});
		}

		void SpvService::syncStopped(const std::string &error) {
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [&error](PeerManager::Listener *listener) {
							  listener->syncStopped(error);
						  });
		}

		void SpvService::txStatusUpdate() {
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [](PeerManager::Listener *listener) {
							  listener->txStatusUpdate();
						  });
		}

		void SpvService::saveBlocks(bool replace, const std::vector<MerkleBlockPtr> &blocks) {

			if (replace) {
				_databaseManager.DeleteAllBlocks(ISO);
			}

			ByteStream ostream;
			std::vector<MerkleBlockEntity> merkleBlockList;
			MerkleBlockEntity blockEntity;
			for (size_t i = 0; i < blocks.size(); ++i) {
				if (blocks[i]->GetHeight() == 0)
					continue;

#ifndef NDEBUG
				if (blocks.size() == 1) {
					Log::debug("{} checkpoint ====> ({},  \"{}\", {}, {});",
							   _peerManager->GetID(),
							   blocks[i]->GetHeight(),
							   blocks[i]->GetHash().GetHex(),
							   blocks[i]->GetTimestamp(),
							   blocks[i]->GetTarget());
				}
#endif

				ostream.Reset();
				blocks[i]->Serialize(ostream);
				blockEntity.blockBytes = ostream.GetBytes();
				blockEntity.blockHeight = blocks[i]->GetHeight();
				merkleBlockList.push_back(blockEntity);
			}
			_databaseManager.PutMerkleBlocks(ISO, merkleBlockList);

			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [replace, &blocks](PeerManager::Listener *listener) {
							  listener->saveBlocks(replace, blocks);
						  });
		}

		void SpvService::savePeers(bool replace, const std::vector<PeerInfo> &peers) {

			if (replace) {
				_databaseManager.DeleteAllPeers(ISO);
			}

			std::vector<PeerEntity> peerEntityList;
			PeerEntity peerEntity;
			for (size_t i = 0; i < peers.size(); ++i) {
				peerEntity.address = peers[i].Address;
				peerEntity.port = peers[i].Port;
				peerEntity.timeStamp = peers[i].Timestamp;
				peerEntityList.push_back(peerEntity);
			}
			_databaseManager.PutPeers(ISO, peerEntityList);

			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [replace, &peers](PeerManager::Listener *listener) {
							  listener->savePeers(replace, peers);
						  });
		}

		void SpvService::onSaveNep5Log(const Nep5LogPtr &nep5Log) {
			Nep5LogEntity logEntity;
			logEntity.txid = nep5Log->GetTxID();
			logEntity.nep5Hash = nep5Log->GetNep5Hash();
			logEntity.fromAddr = nep5Log->GetFrom();
			logEntity.toAddr = nep5Log->GetTo();
			logEntity.value = nep5Log->GetData();
			_databaseManager.PutNep5Log(ISO, logEntity);

			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
			              [&nep5Log](PeerManager::Listener *listener) {
				              listener->onSaveNep5Log(nep5Log);
			              });
		}

		bool SpvService::networkIsReachable() {

			bool reachable = true;
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [&reachable](PeerManager::Listener *listener) {
							  reachable |= listener->networkIsReachable();
						  });
			return reachable;
		}

		void SpvService::txPublished(const std::string &hash, const nlohmann::json &result) {
			std::for_each(_peerManagerListeners.begin(), _peerManagerListeners.end(),
						  [&hash, &result](PeerManager::Listener *listener) {
							  listener->txPublished(hash, result);
						  });
		}

		void SpvService::syncIsInactive(uint32_t time) {
			if (_peerManager->GetReconnectEnableStatus() && _peerManager->ReconnectTaskCount() == 0) {
				Log::info("{} disconnect, reconnect {}s later", _peerManager->GetID(), time);
				if (_reconnectTimer != nullptr) {
					_reconnectTimer->cancel();
					_reconnectTimer = nullptr;
				}

				_peerManager->SetReconnectTaskCount(_peerManager->ReconnectTaskCount() + 1);

				_executor.StopThread();
				_peerManager->SetReconnectEnableStatus(false);
				if (_peerManager->GetConnectStatus() == Peer::Connected) {
					_peerManager->Disconnect();
				}

				_executor.InitThread(BACKGROUND_THREAD_COUNT);
				_peerManager->SetReconnectEnableStatus(true);
				StartReconnect(time);
			}
		}

		size_t SpvService::GetAllTransactionsCount() {
			return _databaseManager.GetAllTransactionsCount(ISO);
		}

		std::vector<CoinBaseUTXOPtr> SpvService::loadCoinBaseUTXOs() {
			std::vector<CoinBaseUTXOPtr> cbs;

			std::vector<CoinBaseUTXOEntityPtr> cbEntitys = _databaseManager.GetAllCoinBase();
			for (size_t i = 0; i < cbEntitys.size(); ++i) {
				CoinBaseUTXOPtr cb(new CoinBaseUTXO());
				cb->SetSpent(cbEntitys[i]->Spent());
				cb->SetHash(uint256(cbEntitys[i]->TxHash()));
				cb->SetBlockHeight(cbEntitys[i]->BlockHeight());
				cb->SetTimestamp(cbEntitys[i]->Timestamp());

				cb->SetAmount(cbEntitys[i]->Amount());
				cb->SetOutputLock(cbEntitys[i]->OutputLock());
				cb->SetAssetID(cbEntitys[i]->AssetID());
				cb->SetProgramHash(cbEntitys[i]->ProgramHash());
				cb->SetIndex(cbEntitys[i]->Index());
				cbs.push_back(cb);
			}

			return cbs;
		}

		// override protected methods
		std::vector<TransactionPtr> SpvService::loadTransactions() {
			std::vector<TransactionPtr> txs;
			std::vector<CoinBaseUTXOEntity> coinBaseEntitys;
			std::set<std::string> spentHashes;
			std::set<std::string> coinBaseHashes;

			std::vector<TransactionEntity> txsEntity = _databaseManager.GetAllTransactions(ISO);

			for (size_t i = 0; i < txsEntity.size(); ++i) {
				TransactionPtr tx(new Transaction());

				ByteStream byteStream(txsEntity[i].buff);
				tx->Deserialize(byteStream);
				tx->SetBlockHeight(txsEntity[i].blockHeight);
				tx->SetTimestamp(txsEntity[i].timeStamp);

				if (tx->IsCoinBase()) {
					coinBaseHashes.insert(tx->GetHash().GetHex());
					for (uint16_t n = 0; n < tx->GetOutputs().size(); ++n) {
						if (_subAccount->ContainsAddress(tx->GetOutputs()[n].GetAddress())) {
							CoinBaseUTXOEntity entity;
							entity.SetAmount(tx->GetOutputs()[n].GetAmount());
							entity.SetOutputLock(tx->GetOutputs()[n].GetOutputLock());
							entity.SetAssetID(tx->GetOutputs()[n].GetAssetID());
							entity.SetProgramHash(tx->GetOutputs()[n].GetProgramHash());
							entity.SetIndex(n);

							entity.SetTxHash(tx->GetHash().GetHex());
							entity.SetBlockHeight(tx->GetBlockHeight());
							entity.SetTimestamp(tx->GetTimestamp());
							entity.SetPayload(nullptr);

							coinBaseEntitys.push_back(entity);
							break;
						}
					}
				} else {
					for (uint16_t n = 0; n < tx->GetInputs().size(); ++n)
						spentHashes.insert(tx->GetInputs()[n].GetTransctionHash().GetHex());

					txs.push_back(tx);
				}
			}

			std::for_each(spentHashes.begin(), spentHashes.end(), [&coinBaseEntitys](const std::string &hash) {
				for (size_t i = 0; i < coinBaseEntitys.size(); ++i) {
					if (coinBaseEntitys[i].TxHash() == hash) {
						coinBaseEntitys[i].SetSpent(true);
						break;
					}
				}
			});

			_databaseManager.PutCoinBase(coinBaseEntitys);

			std::vector<std::string> removeHashes(coinBaseHashes.begin(), coinBaseHashes.end());
			_databaseManager.DeleteTxByHashes(removeHashes);

			return txs;
		}

		std::vector<MerkleBlockPtr> SpvService::loadBlocks() {
			std::vector<MerkleBlockPtr> blocks;

			std::vector<MerkleBlockEntity> blocksEntity = _databaseManager.GetAllMerkleBlocks(ISO);

			for (size_t i = 0; i < blocksEntity.size(); ++i) {
				MerkleBlockPtr block(Registry::Instance()->CreateMerkleBlock(_pluginTypes));
				block->SetHeight(blocksEntity[i].blockHeight);
				ByteStream stream(blocksEntity[i].blockBytes);
				if (!block->Deserialize(stream)) {
					Log::error("{} block deserialize fail", _peerManager->GetID());
				}
				blocks.push_back(block);
			}

			return blocks;
		}

		std::vector<PeerInfo> SpvService::loadPeers() {
			std::vector<PeerInfo> peers;

			std::vector<PeerEntity> peersEntity = _databaseManager.GetAllPeers(ISO);

			for (size_t i = 0; i < peersEntity.size(); ++i) {
				peers.push_back(PeerInfo(peersEntity[i].address, peersEntity[i].port, peersEntity[i].timeStamp));
			}

			return peers;
		}

		std::vector<AssetPtr> SpvService::loadAssets() {
			std::vector<AssetPtr> assets;

			std::vector<AssetEntity> assetsEntity = _databaseManager.GetAllAssets();

			for (size_t i = 0; i < assetsEntity.size(); ++i) {
				ByteStream stream(assetsEntity[i].Asset);
				AssetPtr asset(new Asset());
				if (asset->Deserialize(stream)) {
					asset->SetHash(uint256(assetsEntity[i].AssetID));
					assets.push_back(asset);
				}
			}

			return assets;
		}

		std::vector<Nep5LogPtr> SpvService::loadNep5Logs() {
			std::vector<Nep5LogPtr> nep5Logs;

			std::vector<Nep5LogEntity> nep5LogEntitys = _databaseManager.GetAllLogs();

			for (size_t i = 0; i < nep5LogEntitys.size(); ++i) {
				const Nep5LogEntity *entity = &nep5LogEntitys[i];
				Nep5LogPtr nep5LogPtr(new Nep5Log());
				nep5LogPtr->SetNep5Hash(entity->nep5Hash);
				nep5LogPtr->SetFrom(entity->fromAddr);
				nep5LogPtr->SetTo(entity->toAddr);
				nep5LogPtr->SetData(entity->value);
				nep5LogPtr->SetTxId(entity->txid);
				nep5Logs.push_back(nep5LogPtr);
			}

			return nep5Logs;
		}

		Nep5LogPtr SpvService::getNep5Log(const std::string &txid) {
			Nep5LogEntity nep5LogEntity;
			_databaseManager.GetNep5Log(ISO, txid, nep5LogEntity);
			Nep5LogPtr nep5LogPtr(new Nep5Log());
			nep5LogPtr->SetNep5Hash(nep5LogEntity.nep5Hash);
			nep5LogPtr->SetFrom(nep5LogEntity.fromAddr);
			nep5LogPtr->SetTo(nep5LogEntity.toAddr);
			nep5LogPtr->SetData(nep5LogEntity.value);
			nep5LogPtr->SetTxId(nep5LogEntity.txid);
			return nep5LogPtr;
		}

		const CoreSpvService::PeerManagerListenerPtr &SpvService::createPeerManagerListener() {
			if (_peerManagerListener == nullptr) {
				_peerManagerListener = PeerManagerListenerPtr(
						new WrappedExecutorPeerManagerListener(this, &_executor, &_reconnectExecutor, _pluginTypes));
			}
			return _peerManagerListener;
		}

		const CoreSpvService::WalletListenerPtr &SpvService::createWalletListener() {
			if (_walletListener == nullptr) {
				_walletListener = WalletListenerPtr(new WrappedExecutorWalletListener(this, &_executor));
			}
			return _walletListener;
		}

		void SpvService::RegisterWalletListener(Wallet::Listener *listener) {
			_walletListeners.push_back(listener);
		}

		void SpvService::RegisterPeerManagerListener(PeerManager::Listener *listener) {
			_peerManagerListeners.push_back(listener);
		}

		void SpvService::StartReconnect(uint32_t time) {
			_reconnectTimer = boost::shared_ptr<boost::asio::deadline_timer>(new boost::asio::deadline_timer(
					_reconnectService, boost::posix_time::seconds(time)));

			_peerManager->Lock();
			if (_peerManager->GetPeers().empty()) {
				std::vector<PeerInfo> peers = loadPeers();
				Log::info("{} load {} peers", _peerManager->GetID(), peers.size());
				for (size_t i = 0; i < peers.size(); ++i) {
					Log::debug("{} p[{}]: {}", _peerManager->GetID(), i, peers[i].GetHost());
				}

				_peerManager->SetPeers(peers);
			}
			_peerManager->Unlock();

			_reconnectTimer->async_wait(
					boost::bind(&PeerManager::AsyncConnect, _peerManager.get(), boost::asio::placeholders::error));
			_reconnectService.restart();
			_reconnectService.run_one();
		}

		void SpvService::ResetReconnect() {
			_reconnectTimer->expires_at(_reconnectTimer->expires_at() + boost::posix_time::seconds(_reconnectSeconds));
			_reconnectTimer->async_wait(
					boost::bind(&PeerManager::AsyncConnect, _peerManager.get(), boost::asio::placeholders::error));
		}

	}
}
